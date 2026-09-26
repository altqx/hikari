---
status: accepted
---

# Adopt performance gates on the lower reference class

In the live review on 2026-09-27, altqx chose “Accept gates with lower hardware baseline,” accepting the [performance contract](../qt/performance.md) with unchanged starting release/stretch thresholds and a four-physical-core, 8 GiB RAM, SSD, integrated-graphics, 60 Hz reference class. Release gates block acceptance while stretch targets remain aspirational; changing either the contract or baseline requires an explicit decision. Actual Windows/Linux machine bindings, fixture hashes and measurement calibration remain mandatory and unproved, so this decision neither guarantees every four-core machine nor treats the Python grid sample as a native performance pass.
