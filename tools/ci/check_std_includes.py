#!/usr/bin/env python3
"""Fails when a source file uses a standard-library name without including
the header that declares it.

    python3 tools/ci/check_std_includes.py            (from the repo root)

Why: libstdc++ keeps trimming which standard headers pull in which others.
Code that uses std::transform after including only <string> builds with
GCC 13 (CI's compiler) and fails with GCC 16 ("'transform' is not a member
of 'std'"), found on 2026-09-26 on a contributor's Arch machine. CI can't
run every future compiler, so this checks the rule itself: every std:: name
comes from a header the file includes, directly or through one of this
repository's own headers.

It is a text scan, not a compiler: it knows the common names below, skips
comments and string literals, and follows #include "..." into the repo's
headers. A name it doesn't know is not checked.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCAN_DIRS = ["engine", "games", "tools", "tests", "benchmarks"]
INCLUDE_DIRS = [os.path.join(ROOT, "engine", "include"), os.path.join(ROOT, "engine", "src")]
EXTENSIONS = (".cpp", ".cc", ".h", ".hpp", ".inl")

# header -> names it declares (regex alternatives, matched after "std::").
HEADERS = {
    "algorithm": r"transform|sort|stable_sort|find|find_if|find_if_not|count_if|remove_if|any_of|all_of|none_of|"
                 r"reverse|unique|fill|fill_n|copy_if|min_element|max_element|minmax_element|lower_bound|upper_bound|"
                 r"binary_search|clamp|max|min|minmax|equal|replace|replace_if|for_each|rotate|shuffle|partition|"
                 r"stable_partition|nth_element|partial_sort|copy_n|generate|mismatch|search|adjacent_find|is_sorted|"
                 r"set_intersection|set_difference|set_union|merge|inplace_merge|includes|lexicographical_compare|"
                 r"count|copy|copy_backward|move_backward|next_permutation|equal_range|make_heap|push_heap|pop_heap|"
                 r"sort_heap|ranges::\w+",
    "cctype": r"tolower|toupper|isspace|isdigit|isalpha|isalnum|isupper|islower|isxdigit|isprint|ispunct|iscntrl|isgraph",
    "cstring": r"memcpy|memset|memcmp|memmove|memchr|strlen|strcmp|strncmp|strchr|strrchr|strstr|strcpy|strncpy|"
               r"strerror|strcat|strtok",
    "numeric": r"accumulate|iota|inner_product|partial_sum|adjacent_difference|reduce|transform_reduce|gcd|lcm|"
               r"exclusive_scan|inclusive_scan|midpoint",
    "memory": r"unique_ptr|shared_ptr|weak_ptr|make_unique|make_shared|enable_shared_from_this|addressof",
    "functional": r"function|bind|ref|cref|less|greater|equal_to|plus|invoke|reference_wrapper|hash",
    "utility": r"pair|make_pair|move|forward|swap|exchange|declval|index_sequence|make_index_sequence|in_place|as_const",
    "optional": r"optional|nullopt|make_optional",
    "variant": r"variant|visit|holds_alternative|get_if|monostate",
    "string_view": r"string_view",
    "string": r"string|to_string|stoi|stof|stod|stol|stoul|stoll|stoull|getline|wstring",
    "array": r"array",
    "vector": r"vector",
    "map": r"map|multimap",
    "set": r"set|multiset",
    "unordered_map": r"unordered_map",
    "unordered_set": r"unordered_set",
    "deque": r"deque",
    "list": r"list",
    "queue": r"queue|priority_queue",
    "stack": r"stack",
    "tuple": r"tuple|make_tuple|tie|apply|get",
    "chrono": r"chrono::\w+",
    "thread": r"thread|this_thread::\w+",
    "mutex": r"mutex|lock_guard|unique_lock|scoped_lock|recursive_mutex|call_once|once_flag",
    "shared_mutex": r"shared_mutex|shared_lock",
    "condition_variable": r"condition_variable",
    "atomic": r"atomic|atomic_flag|atomic_thread_fence",
    "sstream": r"stringstream|ostringstream|istringstream",
    "fstream": r"ifstream|ofstream|fstream",
    "iostream": r"cout|cerr|cin|clog",
    "cmath": r"sqrt|pow|sin|cos|tan|atan2|atan|asin|acos|floor|ceil|round|lround|fmod|exp|log|log2|log10|abs|fabs|"
             r"hypot|isnan|isinf|isfinite|copysign|trunc|cbrt|lerp|exp2|signbit|modf|fmin|fmax|fma|nextafter",
    "cstdlib": r"getenv|atoi|atof|strtol|strtoul|strtod|strtof|strtoll|strtoull|exit|abort|malloc|free|qsort|system|"
               r"rand|srand|calloc|realloc|_Exit|quick_exit",
    "cstdio": r"printf|fprintf|snprintf|sprintf|fopen|fclose|fread|fwrite|fgets|fputs|puts|FILE|rename|fflush|"
              r"sscanf|fseek|ftell|vsnprintf|perror",
    "limits": r"numeric_limits",
    "random": r"mt19937|mt19937_64|random_device|uniform_int_distribution|uniform_real_distribution|"
              r"normal_distribution|default_random_engine|bernoulli_distribution|minstd_rand",
    "filesystem": r"filesystem::\w+",
    "regex": r"regex|smatch|regex_match|regex_search|regex_replace",
    "span": r"span",
    "bitset": r"bitset",
    "iterator": r"back_inserter|inserter|istreambuf_iterator|ostream_iterator|distance|advance|next|prev|begin|end|"
                r"size|data|empty|make_move_iterator|reverse_iterator|iterator_traits|ssize|cbegin|cend|rbegin|rend",
    "stdexcept": r"runtime_error|logic_error|invalid_argument|out_of_range|length_error|overflow_error|range_error|"
                 r"domain_error",
    "exception": r"exception_ptr|current_exception|rethrow_exception|terminate|make_exception_ptr",
    "system_error": r"error_code|system_error|errc|generic_category|system_category",
    "bit": r"bit_cast|popcount|countl_zero|countr_zero|bit_width|has_single_bit|bit_ceil|bit_floor|rotl|rotr|endian",
    "future": r"future|promise|async|packaged_task|shared_future",
    "charconv": r"from_chars|to_chars|chars_format",
    "iomanip": r"setw|setprecision|setfill|put_time|quoted",
    "numbers": r"numbers::\w+",
    "type_traits": r"is_same_v|is_same|enable_if_t|decay_t|remove_cv_t|remove_reference_t|remove_cvref_t|"
                   r"is_integral_v|is_floating_point_v|is_arithmetic_v|conditional_t|underlying_type_t|is_enum_v|"
                   r"is_trivially_copyable_v|is_pointer_v|is_convertible_v|is_base_of_v|integral_constant|"
                   r"true_type|false_type|is_signed_v|is_unsigned_v|make_unsigned_t|invoke_result_t|is_invocable_v",
}

# What the standard itself guarantees a header brings along.
IMPLIES = {
    "iostream": {"ostream", "istream"},
    "sstream": {"string", "istream", "ostream"},
    "fstream": {"istream", "ostream"},
    "cinttypes": {"cstdint"},
    "stdexcept": {"exception"},
    "future": {"system_error"},
}
C_HEADERS = {"stdio.h": "cstdio", "stdlib.h": "cstdlib", "string.h": "cstring", "math.h": "cmath",
             "stdint.h": "cstdint", "ctype.h": "cctype", "stddef.h": "cstddef", "time.h": "ctime",
             "inttypes.h": "cinttypes"}

# Names the standard declares in more than one header.
CONTAINERS = {"array", "deque", "forward_list", "list", "map", "regex", "set", "span", "string",
              "string_view", "unordered_map", "unordered_set", "vector"}
ALSO_IN = {
    # std::begin/end/size/data/empty/... come with every container header.
    **{n: CONTAINERS for n in ("begin", "end", "size", "data", "empty", "ssize", "cbegin", "cend",
                               "rbegin", "rend", "reverse_iterator")},
    "swap": {"algorithm", "string", "vector", "map", "unordered_map", "memory", "tuple", "optional",
             "variant", "functional", "array", "set"},
    "move": {"algorithm"},
    "pair": {"map", "unordered_map"},
    "abs": {"cstdlib"},
    "get": {"array", "utility", "variant"},
    "hash": {"string", "string_view", "memory", "optional", "variant", "bitset", "thread", "typeindex",
             "filesystem", "system_error", "unordered_map", "unordered_set"},
    "string": {"sstream", "fstream", "iostream", "stdexcept", "filesystem"},
    "string_view": {"string", "filesystem"},
}

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)
NAME_RES = {h: re.compile(r"\bstd::(" + pat + r")\b") for h, pat in HEADERS.items()}
REMOVE_RE = re.compile(r"\bstd::remove\s*\(([^;]*)")


def strip_comments_and_strings(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    text = re.sub(r'R"(\w*)\(.*?\)\1"', '""', text, flags=re.S)
    return re.sub(r'"(\\.|[^"\\\n])*"', '""', text)


_closure = {}


def included_std_headers(path, visiting=None):
    """Standard headers `path` includes, directly or via repo headers."""
    if path in _closure:
        return _closure[path]
    visiting = visiting or set()
    if path in visiting:
        return set()
    visiting.add(path)
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    found = set()
    for inc in INCLUDE_RE.findall(text):
        if "." not in inc and "/" not in inc:
            found.add(inc)
            found |= IMPLIES.get(inc, set())
        elif inc in C_HEADERS:
            found.add(C_HEADERS[inc])
        else:
            for base in [os.path.dirname(path)] + INCLUDE_DIRS:
                candidate = os.path.normpath(os.path.join(base, inc))
                if candidate.startswith(ROOT) and os.path.isfile(candidate):
                    found |= included_std_headers(candidate, visiting)
                    break
    _closure[path] = found
    return found


def top_level_commas(args):
    depth = 0
    for ch in args:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            if depth == 0:
                return False
            depth -= 1
        elif ch == "," and depth == 0:
            return True
    return False


def missing_in(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        text = strip_comments_and_strings(f.read())
    have = included_std_headers(path)
    missing = {}
    for header, name_re in NAME_RES.items():
        for m in name_re.finditer(text):
            name = m.group(1)
            if header in have or have & ALSO_IN.get(name, set()):
                continue
            missing.setdefault(header, set()).add(name)
    # std::remove: the algorithm (first, last, value) or the file one (path).
    for m in REMOVE_RE.finditer(text):
        header = "algorithm" if top_level_commas(m.group(1)) else "cstdio"
        if header not in have:
            missing.setdefault(header, set()).add("remove")
    return missing


def main():
    problems = 0
    for d in SCAN_DIRS:
        for dirpath, dirnames, filenames in os.walk(os.path.join(ROOT, d)):
            dirnames[:] = sorted(n for n in dirnames if n not in ("external", "third_party", "_deps"))
            for name in sorted(filenames):
                if not name.endswith(EXTENSIONS):
                    continue
                path = os.path.join(dirpath, name)
                for header, names in sorted(missing_in(path).items()):
                    problems += 1
                    print(f"{os.path.relpath(path, ROOT)}: uses std::{', std::'.join(sorted(names))} "
                          f"without #include <{header}>")
    if problems:
        print(f"\n{problems} missing standard include(s). Add each header to the file that uses the name "
              "(newer GCC no longer pulls them in through other headers).")
        return 1
    print("Every std:: name used has its header included.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
