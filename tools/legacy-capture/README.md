# Legacy characterization capture

Records what the legacy wx HikariSub actually does on the [approved-departure fixtures](../../docs/qt/fixtures/approved-departures/README.md), next to the approved expectation, as E1 requires. It builds the legacy app at its pinned baseline commit and never touches a user profile. The legacy app keeps its configuration beside its executable, so every UI capture runs a fresh copy of the package on a private X server.

[`plan.json`](plan.json) gives each fixture case one route:

- **time:** [`time_capture.cpp`](time_capture.cpp) links the real legacy `SubsTime.cpp` and `Timebase.cpp` and evaluates the fixture values. T42-A uses the legacy composition at `SubsGridBase.cpp:534`.
- **save-as:** [`drive.py`](drive.py) opens a scratch copy in the real app under Xvfb and uses Save As (Ctrl+Shift+S) to write a new path. It then hashes the output and records whether it is byte-identical. Failures keep a screenshot and the app's log tail.
- **none:** the case needs a UI flow that is not driven yet. It is recorded as not captured, with the reason; it is not assumed.

Run through [`legacy-capture.yml`](../../.github/workflows/legacy-capture.yml) (manual dispatch). Reviewed observation files are committed under `tests/fixtures/legacy-observations/`. An observation records what the old app did; it is not an expectation for the rewrite. Expectations stay in the fixture manifest and the departure ledger.
