# K63: a nested static_cast copied as `static<int32_t>`

Job `g09-l12` (ENG-G09, applyFilters, 51 dictated lines). Attempt 1 copied
two lines with a cast inside a cast as
`static_cast<float>(rect.offset.x + static<int32_t>(rect.extent.width))`:
the inner `_cast` was dropped, in both lines. The check named the nearest
line and the wanted one (K33), and attempt 2 fixed both. No Claude edit.

Rerun: `job.json`; `wrong-output.gz` is attempt 1's answer.
