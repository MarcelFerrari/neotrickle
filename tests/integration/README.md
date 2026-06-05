# Integration Tests

Install Python test dependencies with:

```bash
python3 -m pip install -r tests/requirements.txt
```

Run the local HTTP characterization tests directly with pytest:

```bash
python3 -m pytest tests/integration
python3 -m pytest tests/integration --ntrickle-mode=plain
python3 -m pytest tests/integration --ntrickle-mode=wrapped \
  --ntrickle-bin=build/bin/ntrickle \
  --ntrickled-bin=build/bin/ntrickled \
  --ntrickle-preload-lib=build/lib/libntrickle_preload.so
```

`--ntrickle-mode=both` is the default. Plain mode runs clients directly; wrapped
mode runs clients through the `ntrickle` launcher, for example
`ntrickle -m standalone -d 64K -- curl ...`. The suite uses a deterministic local
HTTP server and does not require external network access.
