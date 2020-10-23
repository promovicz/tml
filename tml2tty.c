
#undef USE_GETOPT_LONG
#undef USE_WCHAR

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>

#ifdef USE_WCHAR
#include <wcstype.h>
#endif

#include <curses.h>
#include <term.h>

#include <expat.h>

#define TOP 0
#define BOTTOM 1
#define LEFT 2
#define RIGHT 3


static int boolean_decode(const char *c) {
  return strcmp(c, "true") == 0 ? 1 : 0;
}

#ifndef COLOR_DEFAULT
#define COLOR_DEFAULT -1
#endif

struct color {
  char *name;
  int value;
};

struct color colors[] = {
  {"default", COLOR_DEFAULT},
  {"black", COLOR_BLACK},
  {"red", COLOR_RED},
  {"green", COLOR_GREEN},
  {"yellow", COLOR_YELLOW},
  {"blue", COLOR_BLUE},
  {"magenta", COLOR_MAGENTA},
  {"cyan", COLOR_CYAN},
  {"white", COLOR_WHITE},
  {NULL},
};

static int color_decode(const char *c) {
  int i, res = COLOR_DEFAULT;
  long l;
  char *e;
  for(i = 0; colors[i].name; i++) {
    if(strcmp(colors[i].name, c) == 0) {
      res = colors[i].value;
    }
  }
  l = strtol(c, &e, 10);
  if(e != c && !*e) {
    res = (int)l;
  }

  return res;
}

struct attributes {
  struct attributes *next;
  int id;
  int bg;
  int fg;
  int b;
  int i;
  int u;
  int sub;
  int sup;
  int blink;
  int reverse;
  int standout;
};

static void attr_zero(struct attributes *attr) {
  struct attributes *save;
  save = attr->next;
  bzero(attr, sizeof(*attr));
  attr->next = save;
  attr->bg = COLOR_DEFAULT;
  attr->fg = COLOR_DEFAULT;
}

struct processor {
  /* enable body mode */
  int body;
  /* enable debug output */
  int debug;
  /* produce raw terminal output */
  int rawtty;
  /* do not use buffering */
  int unbuffered;

  /* terminal type */
  char *term;
  /* terminal width */
  int termw;

  /* old terminal settings */
  struct termios old_termios;
  /* our terminal settings */
  struct termios our_termios;

  /* command-provided xml */
  char *expression;

  /* input file */
  char *ifile;
  FILE *is;
  int ifd;

  /* output file */
  char *ofile;
  FILE *os;
  int ofd;

  /* output state */
  int ol;
  int oc;
  int ow;

  /* attribute state */
  int attrid;
  struct attributes *attr;

  /* xml parser */
  XML_Parser xml;
};

static struct attributes *attr_push(struct processor *this);
static void attr_apply(struct processor *this);
static void attr_pop(struct processor *this);

static void emit_raw(struct processor *this, const char *buf, size_t len)
{
  if(this->os && !this->unbuffered) {
    size_t res = fwrite(buf, 1, len, this->os);
    if(res < len) {
      perror("fwrite");
      exit(1);
    }
  } else {
    ssize_t res;
    size_t done = 0;
    while(done < len) {
      res = write(this->ofd, buf + done, len - done);
      if(res < 0) {
	perror("write");
	exit(1);
      }
      if(res >= 0) {
	done += res;
      }
    }
  }
}

static void emit_ntimes(struct processor *this, char c, size_t count)
{
  char buf[32];
  memset(buf, c, sizeof(buf));
  while(count) {
    size_t w = count;
    if(w > sizeof(buf))
      w = sizeof(buf);
    emit_raw(this, buf, w);
    count -= w;
  }
}

static void emit_flush(struct processor *this)
{
  if(this->os && !this->unbuffered) {
    fflush(this->os);
  }
}

static void emit_control(struct processor *this, const char *str)
{
  if(str) {
    emit_raw(this, str, strlen(str));
  }
}

#define emit_tparm(this, parm, ...)		  \
  { if (parm) {					  \
      char *str = tiparm(parm, ##__VA_ARGS__);	  \
      if(str) { emit_control(this, str); }	  \
    } }

static void emit_newline(struct processor *this)
{
  struct attributes *attr = this->attr;
  if(this->debug)
    fprintf(stderr, "emit_newline ol=%d oc=%d\n", this->ol, this->oc);
  if(this->attr && this->attr->bg != COLOR_DEFAULT) {
    int spaces;
    struct attributes *attr;
    if(this->oc < this->ow) {
      spaces = this->ow - this->oc;
      emit_ntimes(this, ' ', spaces);
    }
#if 0
    if(this->ow < this->termw && this->rawtty && back_color_erase) {
      spaces == this->termw - this->ow;
      attr = attr_push(this);
      attr_zero(attr);
      attr_apply(this);
      emit_ntimes(this, ' ', spaces);
      attr_pop(this);
    }
#endif
  }
  emit_control(this, this->rawtty ? "\r\n" : "\n");
  emit_flush(this);
  this->ol++; this->oc = 0;
}

static void emit_chars(struct processor *this, const char *str, size_t len)
{
  if(this->debug)
    fprintf(stderr, "emit_chars len=%d\n", (int)len);
  if(str) {
    int i;
    int l = len, s;
    const char *p = str;
    while(l > 0 && *p) {
      i = 0;
      s = this->ow - this->oc;
      if(s < 0) {
	s = 0;
      }

      while(i < l && p[i] && isgraph(p[i])) {
	i++;
      }
      if(i) {
	if(i > s) {
	  i = s;
	}
	emit_raw(this, p, i);
	this->oc += i;
	if(i == s) {
	  emit_newline(this);
	}
	goto next;
      }

      while(i < l && p[i] && p[i] == ' ') {
	i++;
      }
      if(i) {
	int e = i;
	if(e > s) {
	  e = s;
	}
	if(this->oc > 0) {
	  emit_raw(this, p, e);
	  this->oc += i;
	}
	goto next;
      }

    next:
      /* set up for next token */
      p += i;
      l -= i;
    }
  }
}

static void emit_text(struct processor *this, const char *str, size_t len)
{
  if(str) {
    int i;
    int l = len;
    const char *p = str;
    while(l > 0 && *p) {
      i = 0;

      /* printable characters */
      while(i < l && p[i] && (isgraph(p[i]) || p[i] == ' ' || p[i] == '\t')) {
	i++;
      }
      if(i) {
	emit_chars(this, p, i);
	goto next;
      }

      /* ignored whitespace */
      while(i < l && p[i] && (p[i] == '\r'|| p[i] == '\v')) {
	i++;
      }
      if(i) {
	goto next;
      }

      /* newline */
      if(i < l && p[i] && p[i] == '\n') {
	i++;
	emit_newline(this);
	goto next;
      }

      /* any other control chars */
      while(i < l && p[i]) {
	i++;
      }
      if(i) {
	goto next;
      }

      /* should never get here */
      fprintf(stderr, "Tokenizer error!?\n");
      exit(1);

    next:
      /* set up for next token */
      p += i;
      l -= i;
    }
  }
}

static void attr_switch(struct processor *this,
			struct attributes *old, struct attributes *new)
{
  int all = 0, set = 0, flg = 0;

  /* figure out how to reconfigure */
  if(old && new) {
    /* these have no exit call */
    if(old->b && !new->b) {
      set = 1;
    }
    if(old->u && !new->u) {
      set = 1;
    }
    if(old->blink && !new->blink) {
      set = 1;
    }
    if(old->reverse && !new->reverse) {
      set = 1;
    }
    /* if we can't set then clear all */
    if(set && !set_attributes) {
      all = 1;
    }
  }
  if(new) {
    /* default colors are reached by reset */
    if(new->bg == COLOR_DEFAULT && (!old || old->bg != COLOR_DEFAULT)) {
      all = 1;
    }
    if(new->fg == COLOR_DEFAULT && (!old || old->fg != COLOR_DEFAULT)) {
      all = 1;
    }
    /* count enabled flags */
    if(new->b) {
      flg++;
    }
    if(new->u) {
      flg++;
    }
    if(new->blink) {
      flg++;
    }
    if(new->reverse) {
      flg++;
    }
  }
  if(!old && !new) {
    all = 1;
    flg = 0;
  }

  /* reset attributes */
  if(all) {
    emit_tparm(this, exit_attribute_mode);
  }

  /* done if we have no new attrs */
  if(!new) {
    return;
  }

  /* optimizable attributes */
  if(set_attributes && (set || flg > 1)) {
    /* optimized call */
    emit_tparm(this, set_attributes,
	       new->standout, new->u, new->reverse,
	       new->blink, 0, new->b,
	       0, 0, 0);
  } else {
    /* set bold */
    if(new->b) {
      emit_tparm(this, enter_bold_mode);
    }
    /* set underline */
    if(new->u) {
      emit_tparm(this, enter_underline_mode);
    }
    /* set blink */
    if(new->blink) {
      emit_tparm(this, enter_blink_mode);
    }
    /* set reverse */
    if(new->reverse) {
      emit_tparm(this, enter_reverse_mode);
    }
    /* set standout */
    if(new->reverse) {
      emit_tparm(this, enter_standout_mode);
    }
  }

  /* set italic */
  if(new->i && (!old || !old->i)) {
    emit_tparm(this, enter_italics_mode);
  } else if((!old || old->i) && !all) {
    emit_tparm(this, exit_italics_mode);
  }
  /* set superscript */
  if(new->standout && (!old || !old->standout)) {
    emit_tparm(this, enter_standout_mode);
  } else if((!old || old->standout) && !all) {
    emit_tparm(this, exit_standout_mode);
  }
  /* set subscript */
  if(new->sub && (!old || !old->sub)) {
    emit_tparm(this, enter_subscript_mode);
  } else if((!old || old->sub) && !all) {
    emit_tparm(this, exit_subscript_mode);
  }
  /* set superscript */
  if(new->sup && (!old || !old->sup)) {
    emit_tparm(this, enter_superscript_mode);
  } else if((!old || old->sup) && !all) {
    emit_tparm(this, exit_superscript_mode);
  }
  /* set bg color */
  if(new->bg != COLOR_DEFAULT) {
    emit_tparm(this, set_a_background, new->bg);
  }
  /* set fg color */
  if(new->fg != COLOR_DEFAULT) {
    emit_tparm(this, set_a_foreground, new->fg);
  }
}

static struct attributes *attr_push(struct processor *this)
{
  struct attributes *new = calloc(1, sizeof(struct attributes));
  struct attributes *old = this->attr;
  if(!new) {
    abort();
  }
  if(old) {
    memcpy(new, this->attr, sizeof(*new));
    new->next = old;
  }
  new->id = this->attrid++;
  this->attr = new;
  return new;
}

static void attr_apply(struct processor *this)
{
  struct attributes *new = this->attr;
  struct attributes *old = NULL;
  if(new) {
    old = new->next;
  }
  if(old) {
    attr_switch(this, old, new);
  }
}

static void attr_pop(struct processor *this)
{
  struct attributes *old = this->attr;
  struct attributes *new = NULL;
  if(old) {
    new = old->next;
  }
  attr_switch(this, old, new);
  if(old) {
    free(old);
  }
  this->attr = new;
}

static void element_span_start(struct processor *this,
			       const XML_Char *name,
			       const XML_Char **atts)
{
  int i;
  struct attributes *a = attr_push(this);
  for(i = 0; atts[i]; i += 2) {
    const XML_Char *att = atts[i];
    const XML_Char *val = atts[i+1];
    if(strcmp(att, "background") == 0) {
      a->bg = color_decode(val);
    } else if(strcmp(att, "foreground") == 0) {
      a->fg = color_decode(val);
    } else if(strcmp(att, "bg") == 0) {
      a->bg = color_decode(val);
    } else if(strcmp(att, "fg") == 0) {
      a->fg = color_decode(val);
    } else if(strcmp(att, "b") == 0) {
      a->b = boolean_decode(val);
    } else if(strcmp(att, "i") == 0) {
      a->i = boolean_decode(val);
    } else if(strcmp(att, "u") == 0) {
      a->u = boolean_decode(val);
    } else if(strcmp(att, "sub") == 0) {
      a->sub = boolean_decode(val);
    } else if(strcmp(att, "sup") == 0) {
      a->sup = boolean_decode(val);
    } else if(strcmp(att, "blink") == 0) {
      a->blink = boolean_decode(val);
    } else if(strcmp(att, "reverse") == 0) {
      a->reverse = boolean_decode(val);
    } else if(strcmp(att, "standout") == 0) {
      a->standout = boolean_decode(val);
    }
  }
  attr_apply(this);
}

static void element_b_start(struct processor *this,
			    const XML_Char *name,
			    const XML_Char **atts)
{
  attr_push(this)->b = 1;
  attr_apply(this);
}

static void element_i_start(struct processor *this,
			    const XML_Char *name,
			    const XML_Char **atts)
{
  attr_push(this)->i = 1;
  attr_apply(this);
}

static void element_u_start(struct processor *this,
			    const XML_Char *name,
			    const XML_Char **atts)
{
  attr_push(this)->u = 1;
  attr_apply(this);
}

static void element_sub_start(struct processor *this,
			      const XML_Char *name,
			      const XML_Char **atts)
{
  attr_push(this)->sub = 1;
  this->attr->sub = 1;
  attr_apply(this);
}

static void element_sup_start(struct processor *this,
			      const XML_Char *name,
			      const XML_Char **atts)
{
  attr_push(this)->sup = 1;
  attr_apply(this);
}

static void element_blink_start(struct processor *this,
				const XML_Char *name,
				const XML_Char **atts)
{
  attr_push(this)->blink = 1;
  attr_apply(this);
}

static void element_reverse_start(struct processor *this,
				  const XML_Char *name,
				  const XML_Char **atts)
{
  attr_push(this)->reverse = 1;
  attr_apply(this);
}

static void element_standout_start(struct processor *this,
				   const XML_Char *name,
				   const XML_Char **atts)
{
  attr_push(this)->standout = 1;
  attr_apply(this);
}

static void element_fgcolor_start(struct processor *this,
				  const XML_Char *name,
				  const XML_Char **atts)
{
  attr_push(this)->fg = color_decode(name);
  attr_apply(this);
}

static void element_attr_end(struct processor *this)
{
  attr_pop(this);
}


static void element_br_start(struct processor *this,
			     const XML_Char *name,
			     const XML_Char **atts)
{
  emit_newline(this);
}

static void element_p_start(struct processor *this,
			    const XML_Char *name,
			    const XML_Char **atts)
{
  if(this->oc > 0) {
    emit_newline(this);
  }
}

static void element_p_end(struct processor *this)
{
#if 0
  if(this->oc > 0) {
    emit_newline(this);
  }
#endif
}

static void element_tml_start(struct processor *this,
			      const XML_Char *name,
			      const XML_Char **atts)
{
}

static void element_tml_end(struct processor *this)
{
}

struct element {
  char *name;
  void (*element_start)(struct processor *this,
			const XML_Char *name,
			const XML_Char **atts);
  void (*element_end)(struct processor *this);
};

struct element elements[] = {
  /* attributes */
  {"span", element_span_start, element_attr_end},
  {"b", element_b_start, element_attr_end},
  {"i", element_i_start, element_attr_end},
  {"u", element_u_start, element_attr_end},
  {"sub", element_sub_start, element_attr_end},
  {"sup", element_sup_start, element_attr_end},
  {"blink", element_blink_start, element_attr_end},
  {"reverse", element_reverse_start, element_attr_end},
  {"standout", element_standout_start, element_attr_end},
  /* colors */
  {"black", element_fgcolor_start, element_attr_end},
  {"red", element_fgcolor_start, element_attr_end},
  {"green", element_fgcolor_start, element_attr_end},
  {"yellow", element_fgcolor_start, element_attr_end},
  {"blue", element_fgcolor_start, element_attr_end},
  {"magenta", element_fgcolor_start, element_attr_end},
  {"cyan", element_fgcolor_start, element_attr_end},
  {"white", element_fgcolor_start, element_attr_end},
  /* formatting */
  {"br", element_br_start, NULL},
  {"p", element_p_start, element_p_end},
  /* compatibility */
  {"s", NULL, NULL},
  {"tt", NULL, NULL},
  {"big", NULL, NULL},
  {"small", NULL, NULL},
  /* structural */
  {"tml", element_tml_start, element_tml_end},
  /* end */
  {NULL},
};

static void handle_start (struct processor *this,
			  const XML_Char *name,
			  const XML_Char **atts)
{
  int i;
  if(this->debug)
    fprintf(stderr, "%s:start\n", name);
  for(i = 0; elements[i].name; i++) {
    if(strcmp(name, elements[i].name) == 0) {
      if(elements[i].element_start) {
	elements[i].element_start(this, name, atts);
      }
      emit_flush(this);
      return;
    }
  }
}

static void handle_end (struct processor *this,
			const XML_Char *name)
{
  int i;
  if(this->debug)
    fprintf(stderr, "%s:end\n", name);
  for(i = 0; elements[i].name; i++) {
    if(strcmp(name, elements[i].name) == 0) {
      if(elements[i].element_end) {
	elements[i].element_end(this);
      }
      emit_flush(this);
      return;
    }
  }
}

static void handle_cdata (struct processor *this,
			  const XML_Char *s, int len)
{
  if(this->debug)
    fprintf(stderr, "cdata:%d\n", len);
  emit_text(this, s, len);
  emit_flush(this);
}

static int init_term(struct processor *this)
{
  int res, err, fd = this->ofd;

  if(isatty(fd) != 1) {
    fd = -1;
  }

  res = setupterm(this->term, fd, &err);
  if(res != OK) {
    fprintf(stderr, "Error initializing terminal\n");
    return 1;
  }

  this->termw = columns;

  if(this->rawtty && fd > 0) {
    cfmakeraw(&this->our_termios);

    res = tcgetattr(fd, &this->old_termios);
    if(res == -1) {
      perror("tcgetattr");
      return 1;
    }

    res = tcsetattr(fd, 0, &this->our_termios);
    if(res == -1) {
      perror("tcsetattr");
      return 1;
    }
  }

  return 0;
}

static int fini_term(struct processor *this)
{
  int res, fd = this->ofd;

  if(isatty(fd) != 1) {
    fd = -1;
  }

  if(this->rawtty && fd > 0) {
    res = tcsetattr(1, 0, &this->old_termios);
    if(res == -1) {
      perror("tcsetattr");
      return 1;
    }
  }

  return 0;
}

static int init_xml(struct processor *this)
{
  this->xml = XML_ParserCreate(NULL);
  if(!this->xml) {
    fprintf(stderr, "Error initializing expat\n");
    return 1;
  }
  XML_SetUserData(this->xml, this);
  XML_SetElementHandler(this->xml,
			(XML_StartElementHandler)&handle_start,
			(XML_EndElementHandler)&handle_end);
  XML_SetCharacterDataHandler(this->xml,
			      (XML_CharacterDataHandler)&handle_cdata);

  return 0;
}

static int fini_xml(struct processor *this)
{
  return 0;
}

static void usage(const char *argv0) {
  fprintf(stderr, "Usage: %s [<higher magic>]\n", argv0);
}

static int init_opts(struct processor *this,
		     int argc, char **argv)
{
  int opt;

  const char *shortopts = "-:hbdrut:w:i:o:";
#ifdef USE_GETOPT_LONG
  const struct option longopts[] = {
    {"help", 0, NULL, 'h'},
    {"body",       0, &processor->body,      1},
    {"debug",      0, &processor->debug,      1},
    {"rawtty",     0, &processor->rawtty,     1},
    {"unbuffered", 0, &processor->unbuffered, 1},
    {"term",   1, NULL, 't'},
    {"width",  1, NULL, 'w'},
    {"in",     1, NULL, 'i'},
    {"out",    1, NULL, 'o'},
    {NULL,     0, NULL,  0 },
  };
#endif

  optind = 1;
  opterr = 0;
  while(1) {
#ifdef USE_GETOPT_LONG
    opt = getopt_long(argc, argv, shortopts, longopts, NULL);
#else
    opt = getopt(argc, argv, shortopts);
#endif
    if (opt == -1)
      break;

    switch(opt) {
    case 0:
      /* handled by getopt */
      break;
    case 1:
      /* non-option argument */
      this->expression = optarg;
      break;

    case 'b':
      /* body mode */
      this->body = 1;
      break;
    case 'd':
      /* debug mode */
      this->debug = 1;
      break;
    case 'r':
      /* raw mode */
      this->rawtty = 1;
      break;
    case 'u':
      /* unbuffered mode */
      this->unbuffered = 1;
      break;

    case 't':
      /* terminal type */
      this->term = optarg;
      break;
    case 'w':
      /* output width */
      this->ow = (int)strtol(optarg, NULL, 10);
      break;

    case 'i':
      this->ifile = optarg;
      break;
    case 'o':
      this->ofile = optarg;
      break;

    default:
    case 'h':
    case ':':
    case '?':
      usage(argv[0]);
      goto err;

    }
  }

  return 0;

 err:
  return 1;
}

static int process_buf(struct processor *this, const char *buf, size_t len)
{
  enum XML_Status xs;
  enum XML_Error xe;
  xs = XML_Parse(this->xml, buf, len, 0);
  if(xs != XML_STATUS_OK) {
    xe = XML_GetErrorCode(this->xml);
    fprintf(stderr, "XML error: %s\n", XML_ErrorString(xe));
    goto err;
  }
  return 0;
 err:
  return 1;
}

static int process_str(struct processor *this, const char *str)
{
  if(str) {
    return process_buf(this, str, strlen(str));
  } else {
    return 0;
  }
}

static int process_end(struct processor *this)
{
  enum XML_Status xs;
  enum XML_Error xe;
  xs = XML_Parse(this->xml, NULL, 0, 1);
  if(xs != XML_STATUS_OK) {
    xe = XML_GetErrorCode(this->xml);
    fprintf(stderr, "XML error: %s\n", XML_ErrorString(xe));
    return 1;
  }
  return 0;
}

static int process_fd(struct processor *this, int fd)
{
  int res, rd;
  char buf[4096];

  while(1) {
    rd = read(0, buf, sizeof(buf));
    if(rd == -1) {
      if(errno == EAGAIN) {
	continue;
      }
      perror("read");
      goto err;
    }
    res = process_buf(this, buf, rd);
    if(res) {
      goto err;
    }
    if(rd == 0) {
      break;
    }
  }

  return 0;

 err:
  return 1;
}

static int processor_init(struct processor *this) {
  bzero(this, sizeof(*this));
  this->term = getenv("TERM");
  this->termw = -1;
  this->ifd = 0;
  this->is = NULL;
  this->ofd = 1;
  this->os = NULL;
  this->ow = -1;
  return 0;
}

static int processor_run(struct processor *this, int argc, char **argv) {
  int res, ret = 1;
  struct attributes *iattr;

  /* initialize state */
  res = processor_init(this);
  if(res) {
    goto err_init;
  }

  /* parse options */
  res = init_opts(this, argc, argv);
  if(res) {
    goto err_init_opts;
  }

  /* initialize xml parser */
  res = init_xml(this);
  if(res) {
    fprintf(stderr, "Error in XML setup\n");
    goto err_init_xml;
  }

  /* initialize terminal */
  res = init_term(this);
  if(res) {
    fprintf(stderr, "Error in terminal setup\n");
    goto err_init_term;
  }

  /* tweak terminal size for non-raw mode */
  if((isatty(this->ofd) == 1) && !this->rawtty) {
    this->termw -= 1;
  }

  if(this->debug)
    fprintf(stderr, "Terminal type: %s\n", this->term);
  if(this->debug)
    fprintf(stderr, "Terminal width: %d\n", this->termw);

  /* determine output width */
  if(this->ow <= 0) {
    this->ow = this->termw;
  }
  if(this->ow <= 0) {
    this->ow = 80;
  }

  if(this->debug)
    fprintf(stderr, "Output width: %d\n", this->ow);

  /* prepare initial attributes */
  iattr = attr_push(this);
  iattr->fg = COLOR_DEFAULT;
  iattr->bg = COLOR_DEFAULT;

  /* perform processing */
  if(this->expression || this->body) {
    res = process_str(this, "<tml>");
    if(res) {
      goto err_process;
    }
  }
  if(this->expression) {
    res = process_str(this, this->expression);
    if(res) {
      goto err_process;
    }
  } else {
    res = process_fd(this, this->ifd);
    if(res) {
      goto err_process;
    }
  }
  if(this->expression || this->body) {
    res = process_str(this, "</tml>");
    if(res) {
      goto err_process;
    }
  }
  res = process_end(this);
  if(res) {
    goto err_process;
  }

  /* finish attributes */
  attr_pop(this);

  /* emit final newline */
  if(this->oc > 0) {
    emit_newline(this);
  }

  /* flush output */
  emit_flush(this);

  /* done */
  ret = 0;

 err_process:
  res = fini_term(this);
  if(res) {
    fprintf(stderr, "Error finalizing terminal\n");
  }
 err_init_term:
  res = fini_xml(this);
  if(res) {
    fprintf(stderr, "Error finalizing parser\n");
  }
 err_init_xml:
 err_init_opts:
 err_init:

  return ret;
}

int main(int argc, char **argv) {
  struct processor this;
  return processor_run(&this, argc, argv);
}
