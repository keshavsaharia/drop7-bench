---
name: drop7-athena-analytics
description: Query Drop7 website analytics and validated mobile-game history through the bounded Athena workgroups with the drop7-research AWS profile. Use for read-only traffic, app usage, completed-game, score, move, mode, or version analysis.
---

# Drop7 Athena analytics

Use the checked-in helper for source-data reads. It defaults to the production
database and `drop7-research` profile, accepts only one `SELECT` or `WITH`
statement, limits output to 500 rows, and runs in the workgroup with a 1 GiB
scan ceiling:

```sh
npm run analytics:query -- --query \
  'SELECT event_name, count(*) AS events FROM page_views GROUP BY event_name'
```

Use `--file query.sql` for longer SQL, `--stage dev` only when the request is
about development data, and `--output table` for a human-facing quick look.
Keep time predicates tight and aggregate in Athena instead of downloading raw
source objects. Report the query execution ID and scanned bytes with any result.

Before composing a query, read [references/schema.md](references/schema.md) for
the two table schemas and the required completed-game deduplication pattern.

The underlying analytics buckets, Glue catalog, and completed-game archive are
private. Do not mutate Glue or S3 source data, bypass the bounded workgroup, or
publish raw rows, visitor IDs, game IDs, or `tape_json` to the public artifact
bucket. Treat aggregate exports as private until explicitly classified for
public release.
