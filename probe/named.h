#ifndef __NAMED_H__
#define __NAMED_H__

#include <string>




/** Interface for things with names. This is useful when using DPRINTF. */
class Named
{
  private:
    const std::string _name;

  public:
    Named(std::string name_) : _name(name_) { }
    virtual ~Named() = default;

    virtual std::string name() const { return _name; }
};



#endif // __BASE_NAMED_HH__
