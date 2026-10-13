# K58: a focus regex across lines gave the Coder an empty excerpt (Claude's job spec)

Job `g08-mcspawn` (ENG-G08): its `focus` regex spanned three lines (`...;\n        for (...`). The
pipeline matches focus line by line, so nothing matched and the excerpt of ModelModule.cpp was
only `// ...`. All three attempts (6 calls) answered, rightly, that the lines to SEARCH were not
shown; one asked to `cat` the file. No edit was made.

Corrected: focus is one line (`sb.cpu.resize(src.vertices.size());`). Rerun: the job.json here
with the input file from calls.json.gz gives the empty excerpt; with the one-line focus it works.
