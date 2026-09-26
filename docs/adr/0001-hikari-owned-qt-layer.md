---
status: accepted
---

# Own the HikariSub Qt application layer

HikariSub will own its Qt application layer instead of adopting Muse's framework, service container and build conventions. Altqx selected this option in the live Wayfinder review on 2026-09-27 after comparing the [prepared alternatives](https://github.com/altqx/hikari/issues/23#issuecomment-5849085516); it gives the rewritten subtitle core, application services, backend adapters and Qt Quick UI explicit boundaries without coupling the project to another application's framework releases.

The [module contract](../qt/architecture.md) starts ordinary controls with Qt Quick Controls and borrows interaction patterns from MuseScore/Audacity. Reusing an individual external component remains possible after its actual dependencies, licence and maintenance ownership are checked; no Muse source component or docking library is selected by this decision. HikariSub consequently owns action routing, focus/navigation policy, workspace state, theming and service lifetime, including their accessibility checks.

Evidence: [Muse framework investigation](https://github.com/altqx/hikari/blob/d6223fb6ee076bfbf558e6158c1c5fd7c3e167b7/docs/research/muse-framework.md). Decision record: [Adopt the muse framework, or build HikariSub's own app framework?](https://github.com/altqx/hikari/issues/23).
