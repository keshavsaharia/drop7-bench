# Drop7 Athena schema

Production uses database `drop7_production_analytics` and workgroup
`drop7-production-analytics`. Development replaces `production` with `dev` in
both names.

## `page_views`

The Iceberg table contains website page views and batched app interactions.
Useful columns are:

- identity/time: `event_id`, `event_name`, `occurred_at`, `occurred_at_ms`,
  `received_at`, `received_at_ms`;
- navigation/acquisition: `path`, `host`, `referrer_host`, `referrer_channel`;
- coarse client context: `country_code`, `region_code`, `city`, `device_type`,
  `browser_family`, `os_family`, `is_bot`;
- product context: `stage`, `source_application`, `source_platform`,
  `app_version`, `screen`, `properties_json`;
- pseudonymous website counting: `visitor_id`.

Use `occurred_at_ms` for time windows. Filter `event_name = 'page_view'` for web
traffic and normally exclude bots with `is_bot = false`. `visitor_id` is
pseudonymous private data; aggregate it with `approx_distinct` rather than
returning it.

## `game_submissions`

This external JSON table contains independently validated completed mobile
games: `event_id`, `event_name`, `received_at`, `received_at_ms`, `game_id`,
`started_at`, `completed_at`, `source_application`, `source_platform`,
`app_version`, `mode`, `ruleset`, `verified_score`, `verified_level`,
`verified_moves`, `tape_json`, and `stage`.

Firehose retries can deliver the same `event_id` more than once. Deduplicate
before aggregation:

```sql
WITH ranked AS (
  SELECT *, row_number() OVER (
    PARTITION BY event_id ORDER BY received_at_ms ASC
  ) AS delivery_rank
  FROM game_submissions
  WHERE event_name = 'completed_game'
)
SELECT mode, count(*) AS games, avg(verified_score) AS average_score
FROM ranked
WHERE delivery_rank = 1
GROUP BY mode
```

Use `received_at_ms` for ingestion windows or parse the ISO timestamps when the
question depends on play time. `game_id` and `tape_json` are private; do not
return or export them unless the user explicitly requests the raw record for a
legitimate diagnostic and keeps it private.
