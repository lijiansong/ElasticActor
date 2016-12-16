#ifndef _MOLA_OPTIONS_H_
#define _MOLA_OPTIONS_H_

#include <stdint.h>

#include <map>
#include <vector>

namespace mola {
namespace tools {

enum {
  FLAG = 1,
  NUM,
  STR,
};

union OptionValue {
  char *str;
  bool flag;
  uint32_t num;
};

struct Option {
  const char *name;
  int type;
  union OptionValue value;
};

class Options {
  static const std::size_t OPTBUFSIZE = 1024;
protected:
  typedef std::map<std::string, Option> OptionMap;

  void process_defaults();
  void process_one_option(const char * const option);
  int process_first_option(char **bufptr, int lineno = 0,
                           const char *fn = NULL);
  int process_options_from_file(const char *filename);

  void set_option(const char* key, const char *value);
  void set_option(const char* key, uint32_t value);
  void set_option(const char* key, bool value);

  int get_option_type(const char *key);
  Option* optstruct(const char *key, bool install = false);

  void print_package_version();
  void print_config_info();

public:
  explicit Options(Option *nametable, 
                   const char** default_table,
                   const char* sys_conf = nullptr)
    : m_nametable(nametable)
    , m_defaults_table(default_table)
    , m_sys_conf(sys_conf) {}
  virtual ~Options() {}
  virtual void process_options(int argc, char **argv);
  union OptionValue *option(const char* name);
  void usage(const char *argv0);

private:
  OptionMap m_table;
  Option *m_nametable;
  const char ** m_defaults_table;
  const char * m_sys_conf;
};

}
}

#endif // _MOLA_OPTIONS_H_
