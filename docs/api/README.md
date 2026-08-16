# API documentation

English and Chinese Doxygen outputs are generated and audited together:

```bash
python3 scripts/generate_api_docs.py
```

The generated HTML is written to `docs/api/en/html` and
`docs/api/zh/html`. Generated files are intentionally ignored; CI rebuilds
both variants from the current actor-only public headers and rejects retired
API pages or identifiers.
