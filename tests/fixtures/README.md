# Test fixtures

Responses captured from the live services, mostly on 2026-05-17. Files ending
in `.synthetic.json` are hand-made for states that had no live data.
`SHA256SUMS` covers every file here; `make fixtures-check` verifies it.

## SPC static GeoJSON (`www.spc.noaa.gov`)

- `day{1,2,3}otlk_cat.nolyr.geojson`: categorical outlooks. Uppercase
  `LABEL`, `ISSUE`, `VALID`, `EXPIRE`; Polygon and MultiPolygon geometry.
- `day4prob.nolyr.geojson`: Day 4 probability, with `LABEL` as the string
  `"0.15"`.
- `spc_404_no_active_outlook.html`: the HTML page SPC serves with HTTP 404
  when a product is not issued.

## NOAA ArcGIS (`mapservices.weather.noaa.gov`)

Pairs captured as Esri JSON (`.esri.json`, `f=json`) and GeoJSON (`.geojson`,
`f=geojson`) for the parity tests:

- `arcgis_day{1,2,3}_categorical`
- `arcgis_day1_prob_{tornado,hail,wind}` and `arcgis_day2_prob_wind`. `label`
  is a fraction such as `"0.02"`; field names are lowercase.

Single captures:

- `arcgis_day1_torn_conditional_intensity.esri.json`: `label` `"CIG1"`.
- `arcgis_day4_8_nonempty.synthetic.json`: a non-empty Day 4 response.
- `arcgis_day{1,2}_fire_weather.esri.json`: fire weather, with only a numeric
  `dn` and no label.
- `arcgis_mesoscale_discussion.esri.json`: one active discussion.
- `arcgis_mesoscale_discussion_noarea.esri.json`: the `NoArea` placeholder the
  layer returns when no discussion is active (captured 2026-09-25).
- `arcgis_layers_2026-09-03.json`: layer ids, names, and parents of the three
  MapServers. `python3 tools/verify_arcgis_metadata.py` compares it with the
  live services.

## IEM (`mesonet.agron.iastate.edu`)

- `iem_storm_reports.json`: Local Storm Reports, Point geometry.
- `iem_spc_watch.json`: SPC watches.
