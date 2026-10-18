K86 (f02c, 2026-10-18): a job of four blocks in engine/src/Application.cpp. Edit 1 adds two lines before `if (i == 0 && reflReady)`;
K80 had trimmed its long neighbours, so its SEARCH was that one line. The Coder searched a dozen lines around it instead, starting
at its near twin `if (i == 0 && giReady)`, and rewrote them; the repairs went on from there. Failed after 3 attempts.
Rule: a one-line SEARCH gets "that one line alone: put no other line in it", and a near twin line of the file (difflib 0.8) is
named as a different line (diffjobs).
