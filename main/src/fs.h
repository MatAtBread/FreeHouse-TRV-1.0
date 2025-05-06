#ifndef TRV_FS_H
#define TRV_FS_H

class TrvFS {
 protected:
  bool initialised = false;

 public:
  TrvFS();
  virtual ~TrvFS();
  __SIZE_TYPE__ read(const char *name, void *p, __SIZE_TYPE__ size);
  bool write(const char *name, void *p, __SIZE_TYPE__ size);
};
#endif
