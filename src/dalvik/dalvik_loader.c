#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>

#include <dalvik/dalvik_loader.h>
#include <dalvik/dalvik_class.h>
#include <debug.h>
#ifdef PARSER_COUNT
extern int dalvik_method_count;
extern int dalvik_instruction_count;
extern int dalvik_label_count;
extern int dalvik_field_count;
extern int dalvik_class_count;
#endif
int _dalvik_loader_filter(const struct dirent* ent)
{
    if(ent->d_name[0] == '.') return 0;
    return 1;
}
int dalvik_loader_from_directory(const char* path)
{
    int num_dirent;
    struct dirent **result = NULL;
    char *buf = NULL;

    num_dirent = scandir(path, &result, _dalvik_loader_filter, alphasort);

    LOG_DEBUG("Scanning file under %s", path);

    int i;

    if(num_dirent < 0) goto ERR;
    
    size_t bufsize = 1024;
    buf = (char*)malloc(bufsize);

    for(i = 0; i < num_dirent; i ++)
    {
        if(result[i]->d_type == DT_DIR)
        {
            char filename[1024];
            sprintf(filename, "%s/%s", path, result[i]->d_name);
            if(dalvik_loader_from_directory(filename) < 0) goto ERR;
        }
        else 
        {
            LOG_DEBUG("Scanning file %s/%s", path, result[i]->d_name);
            sprintf(buf, "%s/%s", path, result[i]->d_name);
            FILE* fp = fopen(buf, "r");
            fseek(fp, 0, SEEK_END);
            int len = ftell(fp) + 1;
            while(len > bufsize)
            {
                bufsize *= 2;
                buf = (char*)realloc(buf, bufsize);
            }
            if(NULL == fp)
            {
                LOG_ERROR("Can't open file %s", buf);
                goto ERR;
            }
            fseek(fp, 0, SEEK_SET);
            int nbytes;
            if((nbytes = fread(buf, 1, bufsize, fp)) < 0)
            {
                LOG_ERROR("Can't read file");
                goto ERR;
            }
            buf[nbytes] = 0;
            fclose(fp);
            const char *ptr;
            for(ptr = buf; ptr != NULL && ptr[0] != 0;)
            {
                sexpression_t* sexp;
                if(NULL == (ptr = sexp_parse(ptr, &sexp)))
                {
                    sexp_free(sexp);
                    LOG_ERROR("Can't parse S-Expression");
                    goto ERR;
                }
                if(SEXP_NIL == sexp) continue;
                if(NULL == dalvik_class_from_sexp(sexp))
                {
                    sexp_free(sexp);
                    LOG_ERROR("Can't parse class definitions");
                    goto ERR;
                }
                sexp_free(sexp);
                
            }
        }
    }
    if(buf) free(buf);
    for(i = 0; i < num_dirent; i ++)
        free(result[i]);
    if(result) free(result);
    return 0;
ERR:
    if(buf) free(buf);
    for(i = 0; i < num_dirent; i ++)
        free(result[i]);
    if(result) free(result);
    LOG_ERROR("dalvik loader is returninng a failure");
    return -1;
}
#ifdef PARSER_COUNT
void dalvik_loader_summary()
{

    LOG_TRACE("%d classes, %d methods, %d fields, %d labels, %d instructions", 
              dalvik_class_count,
              dalvik_method_count,
              dalvik_field_count,
              dalvik_label_count,
              dalvik_instruction_count);
}
#else
#    define dalvik_loader_summary() /* do nothing */
#endif
