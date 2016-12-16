#include <cassert>
#include <cstring>
#include <cstdlib>

#include <iostream>
#include <fstream>

#include "mola/options.hpp"

using namespace mola::tools;

static inline char* strprefix(char *str, const char *pattern) {
  auto len = strlen(pattern);
  if (len <= strlen(str) && !strncmp(str, pattern, len)) 
    return str + len;
  return nullptr;
}

void Options::process_defaults() {
  const char **opt;

  for (opt = m_defaults_table; *opt; opt++) {
    process_one_option(*opt);
  }

  for (Option *o = m_nametable; o->type; o++) {
    if (option(o->name) == nullptr) {
      std::cerr << "BUG: Option \"" << o->name 
                << "\" has not compiled-in default." << std::endl;
    }
  }
}

void Options::set_option(const char *key, const char *value) {
  assert(get_option_type(key) == STR);
  
  Option *o = optstruct(key, true);
  o->name = strdup(key);
  o->type = STR;
  o->value.str = strdup(value);
}

void Options::set_option(const char *key, uint32_t value) {
  assert(get_option_type(key) == NUM);
  
  Option *o = optstruct(key, true);
  o->name = strdup(key);
  o->type = NUM;
  o->value.num = value;
}

void Options::set_option(const char *key, bool value) {
  assert(get_option_type(key) == FLAG);

  Option *o = optstruct(key, true);
  o->name = strdup(key);
  o->type = FLAG;
  o->value.flag = value;
}

int Options::get_option_type(const char *key) {
  for (Option *o = m_nametable; o->type; ++o) {
    if (strcmp(key, o->name) == 0)
      return o->type;
  }
  return 0; // not found!!
}

void Options::process_one_option(const char *option) {
  char *copy = strdup(option), *equals = NULL;
  uint32_t num;

  if ((equals = strchr(copy, '=')) == nullptr) {
    /* FLAG option */
    const char *trailer = nullptr;
    const char *name;
    bool value;
    if ((trailer = strprefix(copy, "no")) == nullptr) {
      name = copy;
      value = true;
    } else {
      name = trailer;
      value = false;
    }

    if (get_option_type(name) == FLAG)
      set_option(name, value);
    else
      std::cerr << "Unknown option: \"" << name << "\"" << std::endl;
  } else {
    /* NUM or STR option */

    *equals ++ = '\0';
#if 0
    if (*equals == '\0') {
      std::cerr << "Option value missing for \"" << copy << "\"" << std::endl;
      free(copy);
      return;
    }
#endif

    switch (get_option_type(copy)) {
    case STR:
      set_option(copy, equals);
      break;
    case NUM:
      assert(strlen(equals) > 0);
      set_option(copy, (uint32_t)std::stoul(std::string(equals)));
      break;
    default:
      std::cerr << "Unknown option: \"" << copy << "\"" << std::endl;
      break;
    }
  }
  free(copy);
}

int Options::process_first_option(char **bufptr, int lineno, 
                                  const char *filename) {
  char *out, *in, copybuf[OPTBUFSIZE], char_seen;
  bool quoting, quotenext, done, string_done;

  out = *bufptr;
  in = copybuf;
  done = string_done = quoting = quotenext = false;

  while (isspace(*out)) out++;

  while (!done) {
    char_seen = *out++;

    if (char_seen == '\0') {
      done = true;
      string_done = true;
    } else {
      if (quoting) {
        if (char_seen == '\'') quoting = false;
        else *in++ = char_seen;
      }
      else if (quotenext) {
        *in++ = char_seen;
        quotenext = false;
      }
      else {
        if (char_seen == '\'') {
          quoting = true;
        } else if (char_seen == '\\') {
          quotenext = true;
        } else if (char_seen == '#') {
          done = true;
          string_done = true;
        } else if (!isspace(char_seen)) {
          *in++ = char_seen;
        } else {
          done = true;
          string_done = (out[0] == '\0');
        }
      }
    }
  }

  *in++ = '\0';
  *bufptr = out;
  if (quoting) {
    std::cerr << "warning: unterminated quote in config file "
              << filename << " line " << lineno << std::endl;
  }
  if (strlen(copybuf) > 0)
    process_one_option(copybuf);

  return string_done ? 0 : 1;
}

int Options::process_options_from_file(const char *filename) {
  char *buf = new char[OPTBUFSIZE];
  std::ifstream fin(filename);
  if (!fin.is_open()) {
    std::cerr << "cannot open the config file "
              << filename << " for reading" << std::endl;
    return -1;
  }

  char *p = buf;
  int rc, lineno = 1;
  while (!fin.eof()) {
    fin.getline(p, OPTBUFSIZE);
    do {
      rc = process_first_option(&p, lineno, filename);
    } while (rc == 1);
    lineno++;
  }
  
  delete [] buf;
  fin.close();

  return 0;
}

void Options::process_options(int argc, char ** argv) {
  process_defaults();

  char* user_config_filename = nullptr;

  bool read_system_config_file = (m_sys_conf != nullptr);
  std::vector<std::string> command_line_options;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--help") == 0) {
      usage(argv[0]);
      exit(0);
    } else if (strcmp(argv[i], "-n") == 0) {
      read_system_config_file = false;
    } else if (strcmp(argv[i], "-o") == 0) {
      if (argc <= i + 1) {
        std::cerr << "The -o flag requires an argument." << std::endl;
        exit(-1);
      }
      command_line_options.push_back(argv[++i]); 
    } else if (strcmp(argv[i], "-F") == 0) {
      if (argc <= i + 1) {
        std::cerr << "The -F flag requires an argument." << std::endl;
        exit(-1);
      }
      user_config_filename = argv[++i];
    } else if (strcmp(argv[i], "--print-config") == 0) {
      process_defaults();
      print_config_info();
      exit(0);
    } else {
      std::cerr << "Unrecognized option " << argv[i] << std::endl;
      exit(-1);
    }
  }

  if (!read_system_config_file &&
      !user_config_filename) {
    std::cerr << "No config file is provided! " 
              << "Try `--help' for details .." << std::endl;
    exit(-1);
  }

  if (read_system_config_file)
    process_options_from_file(m_sys_conf);

  if (user_config_filename)
    process_options_from_file(user_config_filename);

  for (auto i : command_line_options) {
    process_one_option(i.c_str());
  }
}

Option* Options::optstruct(const char *name, bool install) {
  auto i = m_table.find(name);
  if (i == m_table.end()) {
    if (install) {
      m_table.emplace(name, Option{});
      return &m_table[name];
    } else {
      assert(0 && "cannot insert new table entry!");
      return nullptr;
    }
  }
  return &m_table[name];
}

union OptionValue* Options::option(const char *name) {
  Option *o = optstruct(name);
  if (o != nullptr) return &o->value;
  return nullptr;
}

void Options::usage(const char * argv0) {
  std::cout << "Usage: " << argv0 << " [OPTION]...\n"
            << "\n\t-o OPTION      behave as if OPTION were specified"
            << "\n\t-F FILE        read options from FILE instead of default one"
            << "\n\t-n             do not read the system-wide configuration file"
            << "\n\t--help         display this help message and exit"
            << "\n\t--print-config display compile-time variables and exit"
            << "\n\nReport bugs to <caowei@ict.ac.cn>"
            << std::endl;
}

void Options::print_config_info() {
  static const char* type2str[] = {"", "FLAG", "NUM", "STR"};
  for (int i = 0; ; ++i) {
    Option& o = m_nametable[i];
    if (o.type == 0) break;

    std::cout << "\"" << o.name << "\" [" 
              << type2str[o.type] << "] = ";
    if (o.type == FLAG)
      std::cout << o.value.flag << std::endl;
    else if (o.type == NUM)
      std::cout << o.value.num << std::endl;
    else if (o.value.str != nullptr)
      std::cout << o.value.str << std::endl;
  }
}
