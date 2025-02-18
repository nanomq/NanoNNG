# Command Options

Some _NNG_ utilities need to parse command line options,
and the supplementary function here allows applications that
need the same support to benefit from this.

To make use of this, the supplemental header `<nng/supplemental/util/options.h>`
must be included.

## Parse Command Line Options

```c
typedef struct nng_optspec {
    const char *o_name;  // Long style name (may be NULL for short only)
    int         o_short; // Short option (no clustering!)
    int         o_val;   // Value stored on a good parse (>0)
    bool        o_arg;   // Option takes an argument if true
} nng_optspec;

int nng_opts_parse(int argc, char *const *argv,
                   const nng_optspec *spec, int *val, char **arg, int *idx);
```

The {{i:`nng_opts_parse`}} function is a intended to facilitate parsing
{{i:command-line arguments}}.
This function exists largely to stand in for {{i:`getopt`}} from POSIX systems,
but it is available everywhere that _NNG_ is, and it includes
some capabilities missing from `getopt`.

The function parses arguments from
`main`{{footnote: Parsing argument strings from other sources can be done as well,
although usually then _idx_ will be initialized to zero.}}
(using _argc_ and _argv_),
starting at the index referenced by _idx_.
(New invocations typically set the value pointed to by _idx_ to 1.)

Options are parsed as specified by _spec_ (see [Option Specification](#option-specification).)
The value of the parsed option will be stored at the address indicated by
_val_, and the value of _idx_ will be incremented to reflect the next
option to parse.

> [!TIP]
> For using this to parse command-line like strings that do not include
> the command name itself, set the value referenced by _idx_ to zero instead of one.

If the option had an argument, a pointer to that is returned at the address
referenced by _arg_.

This function should be called repeatedly, until it returns either -1
(indicating the end of options is reached) or a non-zero error code is
returned.

This function may return the following errors:

- [`NNG_EAMBIGUOUS`]: Parsed option matches more than one specification.
- [`NNG_ENOARG`]: Option requires an argument, but one is not present.
- [`NNG_EINVAL`]: An invalid (unknown) argument is present.

### Option Specification

The calling program must first create an array of {{i:`nng_optspec`}} structures
describing the options to be supported.
This structure has the following members:

- `o_name`:

  The long style name for the option, such as "verbose".
  This will be parsed as a [long option](#long-options) on the command line when it is prefixed with two dashes.
  It may be `NULL` if only a [short option](#short-options) is to be supported.

- `o_short`:

  This is a single letter (at present only ASCII letters are supported).
  These options appear as just a single letter, and are prefixed with a single dash on the command line.
  The use of a slash in lieu of the dash is _not_ supported, in order to avoid confusion with path name arguments.
  This value may be set to 0 if no [short option](#short-options) is needed.

- `o_val`:

  This is a numeric value that is unique to this option.
  This value is assigned by the application program, and must be non-zero for a valid option.
  If this is zero, then it indicates the end of the specifications, and the
  rest of this structure is ignored.
  The value will be returned to the caller in _val_ by `nng_opts_parse` when
  this option is parsed from the command line.

- `o_arg`:

  This value should be set to `true` if the option should take an argument.

### Long Options

Long options are parsed from the _argv_ array, and are indicated when
the element being scanned starts with two dashes.
For example, the "verbose" option would be specified as `--verbose` on
the command line.
If a long option takes an argument, it can either immediately follow
the option as the next element in _argv_, or it can be appended to
the option, separated from the option by an equals sign (`=`) or a
colon (`:`).

### Short Options

Short options appear by themselves in an _argv_ element, prefixed by a dash (`-`).
If the short option takes an argument, it can either be appended in the
same element of _argv_, or may appear in the next _argv_ element.

> [!NOTE]
> Option clustering, where multiple options can be crammed together in
> a single _argv_ element, is not supported by this function (yet).

### Prefix Matching

When using long options, the parser will match if it is equal to a prefix
of the `o_name` member of a option specification, provided that it do so
unambiguously (meaning it must not match any other option specification.)

## Example

The following program fragment demonstrates this function.

```c
    enum { OPT_LOGFILE, OPT_VERBOSE };
    char *logfile; // options to be set
    bool verbose;

    static nng_optspec specs[] = {
        {
            .o_name = "logfile",
            .o_short = 'D',
            .o_val = OPT_LOGFILE,
            .o_arg = true,
        }, {
            .o_name = "verbose",
            .o_short = 'V',
            .o_val = OPT_VERBOSE,
            .o_arg = false,
        }, {
            .o_val = 0; // Terminate array
        }
    };

    for (int idx = 1;;) {
        int rv, opt;
        char *arg;
        rv = nng_opts_parse(argc, argv, specs, &opt, &arg, &idx);
        if (rv != 0) {
            break;
        }
        switch (opt) {
        case OPT_LOGFILE:
            logfile = arg;
            break;
        case OPT_VERBOSE:
            verbose = true;
            break;
        }
    }
    if (rv != -1) {
        printf("Options error: %s\n", nng_strerror(rv));
        exit(1);
    }
```

{{#include ../xref.md}}
