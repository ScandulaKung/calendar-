#ifndef CALENDAR_VERSION_HPP
#define CALENDAR_VERSION_HPP

/* A.B.C
   A starts at 1. B counts content changes. C counts bug fixes.
   One content change adds 1 to B. One bug fix adds 1 to C.
   When B or C is 99 and increases again, A increases by 1, that component becomes 0,
   and the other component stays as it is.
   1.12.13 is 12 content changes and 13 fixes.
   2.1.15 is either 101 content changes and 15 fixes, or 1 content change and 115 fixes.
   Change these numbers and the version written in changelog.txt together. */
#define CALENDAR_VERSION_MAJOR 1
#define CALENDAR_VERSION_CONTENT 7
#define CALENDAR_VERSION_FIX 2
#define CALENDAR_VERSION_STRING "1.7.2"
#define CALENDAR_VERSION_WSTRING L"1.7.2"

#endif
