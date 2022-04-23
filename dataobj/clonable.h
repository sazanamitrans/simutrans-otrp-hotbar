#ifndef DATAOBJ_CLONABLE_H
#define DATAOBJ_CLONABLE_H
class clonable {
  public:
    virtual clonable *clone();
};
#endif
