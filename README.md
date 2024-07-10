# MAMBO Trace

Execution tracing for binary lifting.

## Set up

1. Clone and build MAMBO from [here](https://github.com/beehive-lab/mambo). Last tested commit: `c2d51df`.

2. Copy `plugins/trace` to the MAMBO repository.

3. Copy `mambo.patch` to MAMBO repository and run `git apply mambo.patch`.

4. Re-build MAMBO with `make`.

5. Run binary with MAMBO, for example:

```
<mambo-root>/dbm ./a.out
```

## Configuration

Since MAMBO currently does not support passing-in arguments, all settings must be updated ahead of time using `#define` in source files. The following values can be updated:

`ALLOW_CRITICAL_PATH_CHECKS` - Enable checks in the code (e.g., verify that memory allocation was successful).
`PERFORMANCE_MONITORING` - Print tracing time at the end.

Other switches should not be modified, as it may cause unforeseen issues. More information can be found directly inside the source files.

## Status

This repository is a port of the original non-public code and as such is more stable but may lack some features. Most notably multi-threading support has not been ported yet.
