# make_coupler_mosaic MAXXGRID Updates

## Summary

Updated `make_coupler_mosaic` to use runtime-configurable MAXXGRID via the `--maxxgrid` command-line flag, consistent with the changes made to `fregrid`.

## Changes Made

### 1. Command-Line Argument

**Added to long_options array:**
```c
{"maxxgrid", required_argument, NULL, 'x'}
```

**Added case 'x' in switch statement:**
- Parses the `--maxxgrid` argument value
- Validates range: 1e3 to 1e10
- Calls `set_maxxgrid(value)` to configure runtime value
- Shows error if value out of range

### 2. Code Updates (48 replacements)

**malloc() calls (44 replacements):**
```c
// Before
atmxlnd_area[na][nl] = (double *)malloc(MAXXGRID*sizeof(double));

// After  
atmxlnd_area[na][nl] = (double *)malloc(get_maxxgrid()*sizeof(double));
```

**Comparison checks (4 replacements):**
```c
// Before
if(naxo[na][no] > MAXXGRID) mpp_error("...");

// After
if(naxo[na][no] > get_maxxgrid()) mpp_error("...");
```

### 3. Error Messages Updated (4 messages)

**Before:**
- "naxo is greater than MAXXGRID, increase MAXXGRID"
- "naxl is greater than MAXXGRID, increase MAXXGRID"
- "nlxo is greater than MAXXGRID, increase MAXXGRID"
- "nwxo is greater than MAXXGRID, increase MAXXGRID"

**After:**
- "naxo exceeds MAXXGRID limit; increase the value using --maxxgrid flag"
- "naxl exceeds MAXXGRID limit; increase the value using --maxxgrid flag"
- "nlxo exceeds MAXXGRID limit; increase the value using --maxxgrid flag"
- "nwxo exceeds MAXXGRID limit; increase the value using --maxxgrid flag"

### 4. Usage Documentation

Added to usage[] array:
```
--maxxgrid #          Set maximum exchange grid size. Controls memory allocation
                      for remapping operations. Default: 1e6 (1 million). Increase
                      if you encounter 'exceeds MAXXGRID' errors with high-resolution
                      grids. Valid range: 1e3 to 1e10.
```

## Usage Examples

```bash
# Default MAXXGRID (1e6)
make_coupler_mosaic --atmos_mosaic atm.nc --ocean_mosaic ocn.nc --ocean_topog topo.nc

# Custom MAXXGRID for high-resolution grids
make_coupler_mosaic --atmos_mosaic atm.nc --ocean_mosaic ocn.nc \
                    --ocean_topog topo.nc --maxxgrid 1e7

# Very high resolution
make_coupler_mosaic --atmos_mosaic atm.nc --ocean_mosaic ocn.nc \
                    --ocean_topog topo.nc --maxxgrid 5e7
```

## File Modified

- `src/make-coupler-mosaic/make_coupler_mosaic.c`
  - 63 insertions(+), 48 deletions(-)
  - 48 MAXXGRID replacements with get_maxxgrid()
  - 4 error messages updated
  - 1 command-line option added
  - Usage documentation added

## Consistency with fregrid

Both tools now:
- Use the same global `g_maxxgrid` variable (via get_maxxgrid())
- Accept `--maxxgrid` flag with same validation (1e3 to 1e10)
- Use same default value (1e6)
- Have consistent error messages referencing the flag
- Share the same implementation in `lib/libfrencutils/create_xgrid.c`

## Testing

- ✅ Syntax-checked (compiles with correct includes)
- ✅ All MAXXGRID occurrences replaced (except MAXXGRIDFILE which is different)
- ✅ Command-line parsing added correctly
- ✅ Error messages updated
- ✅ Usage documentation added

## Notes

- `MAXXGRIDFILE` is a different constant (for file arrays) and was left unchanged
- The tool uses the same `set_maxxgrid()` and `get_maxxgrid()` functions from `create_xgrid.c`
- Both `fregrid` and `make_coupler_mosaic` now share consistent MAXXGRID behavior
