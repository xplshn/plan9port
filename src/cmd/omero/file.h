#ifndef FILE_H
#define FILE_H
struct Filelist {
    File *f;
    Filelist *link;
};
#endif
