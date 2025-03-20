#ifdef picorb_NO_STDIO
# error picoruby-bin-picoruby conflicts 'picorb_NO_STDIO' in your build configuration
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

//#include <mrubyc.h>
#include <mruby.h>

#include <mrc_common.h>
#include <mrc_ccontext.h>
#include <mrc_compile.h>
#include <mrc_dump.h>
#include <picogem_init.c>

#if defined(_WIN32) || defined(_WIN64)
# include <io.h> /* for setmode */
# include <fcntl.h>
#endif

#ifdef _WIN32
#include <stdlib.h>
#include <malloc.h>
#include <windows.h>
char*
picorb_utf8_from_locale(const char *str, int len)
{
  wchar_t* wcsp;
  char* mbsp;
  int mbssize, wcssize;

  if (len == 0)
    return strdup("");
  if (len == -1)
    len = (int)strlen(str);
  wcssize = MultiByteToWideChar(GetACP(), 0, str, len,  NULL, 0);
  wcsp = (wchar_t*) mrbc_raw_alloc((wcssize + 1) * sizeof(wchar_t));
  if (!wcsp)
    return NULL;
  wcssize = MultiByteToWideChar(GetACP(), 0, str, len, wcsp, wcssize + 1);
  wcsp[wcssize] = 0;

  mbssize = WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR) wcsp, -1, NULL, 0, NULL, NULL);
  mbsp = (char*) mrbc_raw_alloc((mbssize + 1));
  if (!mbsp) {
    free(wcsp);
    return NULL;
  }
  mbssize = WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR) wcsp, -1, mbsp, mbssize, NULL, NULL);
  mbsp[mbssize] = 0;
  free(wcsp);
  return mbsp;
}
#define picorb_utf8_free(p) free(p)
#else
#define picorb_utf8_from_locale(p, l) ((char*)(p))
#define picorb_utf8_free(p)
#endif

#ifndef HEAP_SIZE
#define HEAP_SIZE (1024 * 6400 - 1)
#endif
static uint8_t mrbc_heap[HEAP_SIZE];

#ifndef MAX_REGS_SIZE
#define MAX_REGS_SIZE 255
#endif

struct _args {
  char *cmdline;
  const char *fname;
  mrc_bool mrbfile      : 1;
  mrc_bool check_syntax : 1;
  mrc_bool verbose      : 1;
  mrc_bool version      : 1;
  mrc_bool debug        : 1;
  int argc;
  char **argv;
  int libc;
  char **libv;
};

struct options {
  int argc;
  char **argv;
  char *program;
  char *opt;
  char short_opt[2];
};

#define PICORUBY_VERSION "3.3.0"

static void
picorb_show_version(void)
{
  fprintf(stdout, "picoruby %s\n", PICORUBY_VERSION);
}

static void
picorb_show_copyright(void)
{
  picorb_show_version();
  fprintf(stdout, "picoruby is a lightweight implementation of the Ruby language.\n");
  fprintf(stdout, "picoruby is based on mruby/c.\n");
  fprintf(stdout, "picoruby is released under the MIT License.\n");
}

static void
usage(const char *name)
{
  static const char *const usage_msg[] = {
  "switches:",
  "-b           load and execute RiteBinary (mrb) file",
  "-c           check syntax only",
  "-d           set debugging flags (set $DEBUG to true)",
  "-e 'command' one line of script",
  "-r library   load the library before executing your script",
  "-v           print version number, then run in verbose mode",
  "--verbose    run in verbose mode",
  "--version    print the version",
  "--copyright  print the copyright",
  NULL
  };
  const char *const *p = usage_msg;

  printf("Usage: %s [switches] [programfile] [arguments]\n", name);
  printf("  programfile can be comma-linked multiple files like `a.rb,b.rb,c.rb`\n");
  while (*p)
    printf("  %s\n", *p++);
}

/*
 * In order to be recognized as a `.mrb` file, the following three points must be satisfied:
 * - File starts with "RITE"
 */
static mrc_bool
picorb_mrb_p(const char *path)
{
  FILE *fp;
  char buf[4];
  mrc_bool ret = FALSE;

  if ((fp = fopen(path, "rb")) == NULL) {
    return FALSE;
  }
  if (fread(buf, 1, sizeof(buf), fp) == sizeof(buf)) {
    if (memcmp(buf, "RITE", 4) == 0) {
      ret = TRUE;
    }
  }
  fclose(fp);
  return ret;
}

static void
options_init(struct options *opts, int argc, char **argv)
{
  opts->argc = argc;
  opts->argv = argv;
  opts->program = *argv;
  *opts->short_opt = 0;
}

static const char *
options_opt(struct options *opts)
{
  /* concatenated short options (e.g. `-cv`) */
  if (*opts->short_opt && *++opts->opt) {
   short_opt:
    opts->short_opt[0] = *opts->opt;
    opts->short_opt[1] = 0;
    return opts->short_opt;
  }

  while (++opts->argv, --opts->argc) {
    opts->opt = *opts->argv;

    /*  not start with `-`  || `-` */
    if (opts->opt[0] != '-' || !opts->opt[1]) return NULL;

    if (opts->opt[1] == '-') {
      /* `--` */
      if (!opts->opt[2]) {
        opts->argv++, opts->argc--;
        return NULL;
      }
      /* long option */
      opts->opt += 2;
      *opts->short_opt = 0;
      return opts->opt;
    }
    else {
      /* short option */
      opts->opt++;
      goto short_opt;
    }
  }
  return NULL;
}

static const char *
options_arg(struct options *opts)
{
  if (*opts->short_opt && opts->opt[1]) {
    /* concatenated short option and option argument (e.g. `-rLIBRARY`) */
    *opts->short_opt = 0;
    return opts->opt + 1;
  }
  --opts->argc, ++opts->argv;
  return opts->argc ? *opts->argv : NULL;
}

static char *
dup_arg_item(const char *item)
{
  size_t buflen = strlen(item) + 1;
  char *buf = (char*)mrbc_raw_alloc(buflen);
  memcpy(buf, item, buflen);
  return buf;
}

static int
parse_args(int argc, char **argv, struct _args *args)
{
  static const struct _args args_zero = { 0 };
  struct options opts[1];
  const char *opt, *item;

  *args = args_zero;
  options_init(opts, argc, argv);
  while ((opt = options_opt(opts))) {
    if (strcmp(opt, "b") == 0) {
      args->mrbfile = TRUE;
    }
    else if (strcmp(opt, "c") == 0) {
      args->check_syntax = TRUE;
    }
    else if (strcmp(opt, "d") == 0) {
      args->debug = TRUE;
    }
    else if (strcmp(opt, "e") == 0) {
      if ((item = options_arg(opts))) {
        if (!args->cmdline) {
          args->cmdline = dup_arg_item(item);
        }
        else {
          size_t cmdlinelen;
          size_t itemlen;

          cmdlinelen = strlen(args->cmdline);
          itemlen = strlen(item);
          args->cmdline = (char*)mrbc_raw_realloc(args->cmdline, cmdlinelen + itemlen + 2);
          args->cmdline[cmdlinelen] = '\n';
          memcpy(args->cmdline + cmdlinelen + 1, item, itemlen + 1);
        }
      }
      else {
        fprintf(stderr, "%s: No code specified for -e\n", opts->program);
        return EXIT_FAILURE;
      }
    }
    else if (strcmp(opt, "h") == 0) {
      usage(opts->program);
      exit(EXIT_SUCCESS);
    }
    else if (strcmp(opt, "r") == 0) {
      if ((item = options_arg(opts))) {
        if (args->libc == 0) {
          args->libv = (char**)mrbc_raw_alloc(sizeof(char*));
        }
        else {
          args->libv = (char**)mrbc_raw_realloc(args->libv, sizeof(char*) * (args->libc + 1));
        }
        args->libv[args->libc++] = dup_arg_item(item);
      }
      else {
        fprintf(stderr, "%s: No library specified for -r\n", opts->program);
        return EXIT_FAILURE;
      }
    }
    else if (strcmp(opt, "v") == 0) {
      if (!args->verbose) {
        picorb_show_version();
        args->version = TRUE;
      }
      args->verbose = TRUE;
    }
    else if (strcmp(opt, "version") == 0) {
      picorb_show_version();
      exit(EXIT_SUCCESS);
    }
    else if (strcmp(opt, "verbose") == 0) {
      args->verbose = TRUE;
    }
    else if (strcmp(opt, "copyright") == 0) {
      picorb_show_copyright();
      exit(EXIT_SUCCESS);
    }
    else {
      fprintf(stderr, "%s: invalid option %s%s (-h will show valid options)\n",
              opts->program, opt[1] ? "--" : "-", opt);
      return EXIT_FAILURE;
    }
  }

  argc = opts->argc; argv = opts->argv;
  if (args->cmdline == NULL) {
    if (*argv == NULL) {
      if (args->version) exit(EXIT_SUCCESS);
      args->cmdline = (char *)"-";
      args->fname = "-";
    }
    else {
      args->fname = *argv;
      args->cmdline = argv[0];
      argc--; argv++;
    }
  }
  args->argv = (char **)mrbc_raw_alloc(sizeof(char*) * (argc + 1));
  memcpy(args->argv, argv, (argc+1) * sizeof(char*));
  args->argc = argc;

  return EXIT_SUCCESS;
}

static void
cleanup(struct _args *args)
{
  if (!args->fname)
    mrbc_raw_free(args->cmdline);
  mrbc_raw_free(args->argv);
  if (args->libc) {
    while (args->libc--) {
      mrbc_raw_free(args->libv[args->libc]);
    }
    mrbc_raw_free(args->libv);
  }
//  mrb_close(mrb);
}

static mrc_bool
picorb_undef_p(mrbc_value *v)
{
  if (!v) return TRUE;
  return v->tt == MRBC_TT_EMPTY;
}

static void
picorb_vm_init(mrbc_vm *vm)
{
  mrbc_init(mrbc_heap, HEAP_SIZE);
  picoruby_init_require(vm);
}

static void
lib_run(mrc_ccontext *c, mrbc_vm *vm, uint8_t *mrb)
{
  if (mrbc_load_mrb(vm, mrb) != 0) {
    fprintf(stderr, "mrbc_load_mrb failed\n");
    return;
  }
  mrbc_vm_begin(vm);
  mrbc_vm_run(vm);
  mrbc_vm_end(vm);
}

static mrc_irep *
picorb_load_rb_file_cxt(mrc_ccontext *c, const char *fname, uint8_t **source)
{
  char *filenames[2];// = (char**)mrbc_raw_alloc(sizeof(char*) * 2);
  filenames[0] = (char *)fname;
  filenames[1] = NULL;
  mrc_irep *irep = mrc_load_file_cxt(c, (const char **)filenames, source);
  if (irep == NULL) {
    fprintf(stderr, "irep load error\n");
  }
  return irep;
}

static void
picorb_print_error(void)
{
  //TODO
}

int
main(int argc, char **argv)
{
  picorb_vm_init(NULL);

  int n = -1;
  struct _args args;
  mrbc_value ARGV;
  mrc_irep *irep = NULL;

  n = parse_args(argc, argv, &args);
  if (n == EXIT_FAILURE || (args.cmdline == NULL)) {
    cleanup(&args);
    return n;
  }

  ARGV = mrbc_array_new(NULL, args.argc);
  for (int i = 0; i < args.argc; i++) {
    char* utf8 = picorb_utf8_from_locale(args.argv[i], -1);
    if (utf8) {
      mrbc_value str = mrbc_string_new(NULL, utf8, strlen(utf8));
      mrbc_array_push(&ARGV, &str);
      picorb_utf8_free(utf8);
    }
  }
  mrbc_set_const(mrbc_str_to_symid("ARGV"), &ARGV);
  mrbc_value debug = mrbc_bool_value(args.debug);
  mrbc_set_global(mrbc_str_to_symid("$DEBUG"), &debug);

  /* Set $0 */
  const char *cmdline;
  if (args.fname) {
    cmdline = args.cmdline ? args.cmdline : "-";
  }
  else {
    cmdline = "-e";
  }
  mrbc_value cmd = mrbc_string_new(NULL, cmdline, strlen(cmdline));
  mrbc_set_global(mrbc_str_to_symid("$0"), &cmd);

  uint8_t *source = NULL;

  mrbc_vm *lib_vm_list[args.libc];
  int lib_vm_list_size = 0;
  uint8_t *lib_mrb_list[args.libc];
  int lib_mrb_list_size = 0;

  /* Load libraries */
  for (int i = 0; i < args.libc; i++) {
    if (!picoruby_load_model_by_name(args.libv[i])) {
      fprintf(stderr, "cannot load library: %s\n", args.libv[i]);
      exit(EXIT_FAILURE);
    }

    mrc_ccontext *c = mrc_ccontext_new(NULL);
    if (args.verbose) c->dump_result = TRUE;
    if (args.check_syntax) c->no_exec = TRUE;
    mrc_ccontext_filename(c, args.libv[i]);

    irep = NULL;
    uint8_t *mrb = NULL;

    if (picorb_mrb_p(args.libv[i])) {
      size_t size;
      FILE *fp = fopen(args.libv[i], "rb");
      if (fp == NULL) {
        fprintf(stderr, "cannot open file: %s\n", args.libv[i]);
        exit(EXIT_FAILURE);
      }
      fseek(fp, 0, SEEK_END);
      size = ftell(fp);
      fseek(fp, 0, SEEK_SET);
      mrb = (uint8_t *)mrbc_raw_alloc(size);
      if (fread(mrb, 1, size, fp) != size) {
        fprintf(stderr, "cannot read file: %s\n", args.libv[i]);
        exit(EXIT_FAILURE);
      }
      fclose(fp);
    }
    else {
      source = NULL;
      irep = picorb_load_rb_file_cxt(c, args.libv[i], &source);
    }

    if (irep) {
      size_t mrb_size = 0;
      int result;
      result = mrc_dump_irep(c, irep, 0, &mrb, &mrb_size);
      if (result != MRC_DUMP_OK) {
        fprintf(stderr, "irep dump error: %d\n", result);
        exit(EXIT_FAILURE);
      }
    }

    if (mrb) {
      lib_mrb_list[lib_mrb_list_size++] = mrb;
      if (irep) mrc_irep_free(c, irep);
      mrc_ccontext_free(c);
      mrbc_vm *libvm = mrbc_vm_open(NULL);
      lib_vm_list[lib_vm_list_size++] = libvm;
      lib_run(c, libvm, mrb);
    }
    if (source) {
      // TODO refactor
      mrbc_raw_free(source);
      source = NULL;
    }
  }

  /*
   * [tasks]
   * args.fname possibly looks like `a.rb,b.rb,c.rb` (comma separated file names)
   */
  int taskc = 1;
  if (args.fname) {
    for (int i = 0; args.fname[i]; i++) {
      if (args.fname[i] == ',') {
        taskc++;
      }
    }
  }
  else {
    /* -e option */
  }
  uint8_t *task_mrb_list[taskc];
  int tasks_mrb_list_size = 0;
  mrbc_tcb *tcb_list[taskc];
  int tcb_list_size = 0;
  char *fnames[taskc];
  char *token = strtok((char *)cmdline, ",");
  int index = 0;
  while (token != NULL) {
    fnames[index] = mrbc_raw_alloc(strlen(token) + 1);
    if (fnames[index] == NULL) {
      fprintf(stderr, "Failed to allocate memory");
      exit(EXIT_FAILURE);
    }
    strcpy(fnames[index], token);
    token = strtok(NULL, ",");
    index++;
  }
  for (int i = 0; i < taskc; i++) {
    /* set program file name */
    mrc_ccontext *c = mrc_ccontext_new(NULL);
    if (args.verbose) c->dump_result = TRUE;
    if (args.check_syntax) c->no_exec = TRUE;
    mrc_ccontext_filename(c, fnames[i]);

    uint8_t *mrb = NULL;
    size_t mrb_size = 0;

    /* Load program */
    if (args.mrbfile || picorb_mrb_p(fnames[i])) {
      irep = NULL;
    }
    else if (args.fname) {
      // TODO refactor
      source = mrbc_raw_alloc(sizeof(uint8_t) * 2);
      source[0] = 0x0;
      source[1] = 0x0;
      irep = picorb_load_rb_file_cxt(c, fnames[i], &source);
    }
    else {
      char* utf8 = picorb_utf8_from_locale(args.cmdline, -1);
      if (!utf8) abort();
      irep = mrc_load_string_cxt(c, (const uint8_t **)&utf8, strlen(utf8));
      picorb_utf8_free(utf8);
    }

    if (!irep) { // mrb file
      FILE *fp = fopen(fnames[i], "rb");
      if (fp == NULL) {
        fprintf(stderr, "cannot open file: %s\n", fnames[i]);
        exit(EXIT_FAILURE);
      }
      fseek(fp, 0, SEEK_END);
      mrb_size = ftell(fp);
      fseek(fp, 0, SEEK_SET);
      mrb = (uint8_t *)mrbc_raw_alloc(mrb_size);
      if (fread(mrb, 1, mrb_size, fp) != mrb_size) {
        fprintf(stderr, "cannot read file: %s\n", fnames[i]);
        exit(EXIT_FAILURE);
      }
      fclose(fp);
    }
    else {
      int result;
      result = mrc_dump_irep(c, irep, 0, &mrb, &mrb_size);
      if (result != MRC_DUMP_OK) {
        fprintf(stderr, "irep dump error: %d\n", result);
        exit(EXIT_FAILURE);
      }
    }

    task_mrb_list[tasks_mrb_list_size++] = mrb;

    if (irep) {
      mrc_ccontext_free(c);
      mrc_irep_free(c, irep);
    }
    if (source) {
      mrc_free(source);
      source = NULL;
    }

    mrbc_tcb *tcb = mrbc_create_task(mrb, NULL);
    if (!tcb) {
      fprintf(stderr, "mrbc_create_task failed\n");
      exit(EXIT_FAILURE);
    }
    else {
      tcb_list[tcb_list_size++] = tcb;
    }

    if (args.check_syntax) {
      printf("Syntax OK: %s\n", fnames[i]);
    }
  }

  /* run tasks */
  if (!args.check_syntax && mrbc_run() != 0) {
    if (!picorb_undef_p(NULL)) {
      picorb_print_error();
    }
    n = EXIT_FAILURE;
  }

  for (int i = 0; i < lib_vm_list_size; i++) {
    mrbc_vm_close(lib_vm_list[i]);
  }
  for (int i = 0; i < lib_mrb_list_size; i++) {
    mrbc_raw_free(lib_mrb_list[i]);
  }
  for (int i = 0; i < tasks_mrb_list_size; i++) {
    mrbc_raw_free(task_mrb_list[i]);
  }
  for (int i = 0; i < taskc; i++) {
    mrbc_raw_free(fnames[i]);
  }
  for (int i = 0; i < tcb_list_size; i++) {
    mrbc_vm_close(&tcb_list[i]->vm);
  }
  cleanup(&args);

  return n;
}
