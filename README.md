# MAMBO Trace

Execution tracing for binary lifting.

## Set up

1. Clone and build MAMBO from [here](https://github.com/beehive-lab/mambo). Last tested commit: `5748b2a`.

2. Copy `plugins/trace` to the MAMBO repository.

3. Copy `mambo.patch` to MAMBO repository and run `git apply mambo.patch`.

4. Re-build MAMBO with `make`.

5. Run binary with MAMBO, for example:

```
<mambo-root>/dbm ./a.out
```
