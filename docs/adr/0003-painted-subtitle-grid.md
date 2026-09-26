---
status: accepted
---

# Use a custom painted subtitle grid

altqx chose [“Use painted grid”](https://github.com/altqx/hikari/issues/27#issuecomment-5849499565), accepting a custom painted grid as the Qt rewrite's renderer direction over the earlier TableView-first research recommendation. The [Python comparison spike](https://github.com/altqx/hikari/blob/d99b429e7a6e16107fa6d6bf80cc5fba04a6264d/HikariSub/prototypes/qml-grid/README.md) is exploratory evidence, not a production API/language choice or proof of optimized C++ performance. The [grid specification](../qt/ux/subtitle-grid.md) preserves document identity and interaction semantics and requires custom native accessibility work plus the still-open performance and test-strategy gates.
