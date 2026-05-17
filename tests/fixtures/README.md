# SPC characterization fixtures

All payloads here were captured **live** on 2026-05-17 from the real upstream
SPC sources (no synthetic/invented data). They pin the byte-identity gate for
the spc-cpp extraction (Workstream C) and double as the spc-cpp parity corpus.

## Static `www.spc.noaa.gov` GeoJSON (the 7-feed fallback path)

- `day1otlk_cat.nolyr.geojson`, `day2otlk_cat.nolyr.geojson`,
  `day3otlk_cat.nolyr.geojson` — live day 1/2/3 categorical outlooks
  (uppercase `LABEL`/`ISSUE`/`VALID`/`EXPIRE`, Polygon + MultiPolygon).
- `day4prob.nolyr.geojson` — live day4-8 experimental probabilistic outlook
  (`LABEL` as `"0.15"` numeric-string).
- `spc_404_no_active_outlook.html` — the literal HTML body SPC returns with
  HTTP 404 when no active outlook exists (e.g. day-1 probabilistic overnight).
  Characterizes `main.cpp`'s 404 -> clear-rows path. The static
  `day{1,2}probotlk_*.nolyr.geojson` feeds were 404 at capture time (the
  documented normal overnight state) so the probabilistic GeoJSON corpus comes
  from the ArcGIS `f=geojson` mirror of the same upstream product (below).

## ArcGIS MapServer (`mapservices.weather.noaa.gov`, the primary path)

`SPC_wx_outlks` MapServer, captured both ways for the Esri-vs-GeoJSON parity
test (`*.esri.json` = `f=json` Esri rings; `*.geojson` = `f=geojson`):

- `arcgis_day{1,2,3}_categorical.{esri.json,geojson}`
- `arcgis_day1_prob_{tornado,hail,wind}.{esri.json,geojson}`,
  `arcgis_day2_prob_any.{esri.json,geojson}` — active probabilistic isopleths
  (`dn` numeric percent + `label` as `"0.02"`; lowercase `valid`/`expire`).
- `arcgis_day1_torn_conditional_intensity.esri.json` — net-new conditional
  intensity (`label` `"CIG1"`).
- `arcgis_day4_8_convective.esri.json` — empty FeatureSet (0 features) at
  capture time; pins the empty-collection path.
- `arcgis_day{1,2}_fire_weather.esri.json` — `SPC_firewx` MapServer.
- `arcgis_mesoscale_discussion.esri.json` — `spc_mesoscale_discussion`
  MapServer (raw `name`/`folderpath`/`popupinfo`; narrative NOT parsed).

## IEM archive (`mesonet.agron.iastate.edu`, best-effort backfill)

- `iem_storm_reports.json` — LSR GeoJSON FeatureCollection (Point geometry).
- `iem_spc_watch.json` — active/historical SPC watch GeoJSON.
- `iem_spcoutlook_torn.json` — IEM's tabular SPC outlook index JSON.
