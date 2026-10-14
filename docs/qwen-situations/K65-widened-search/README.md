K65 (winpub, 2026-10-14): a dictated 2-line SEARCH block (the last two lines of the upload-artifact step) came back widened by the
three lines above it, while the REPLACE block stayed as dictated, so the step's `- uses:`, `if: always()` and `with:` lines were deleted.
The check only looked for the added lines and passed. Fix: diffjobs.py records the lines each job may remove and chk_g09.py fails
when any other line is deleted ("these lines were deleted but must stay"). Rerun: the same job with the new check.
