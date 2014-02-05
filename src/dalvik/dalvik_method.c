#include <dalvik/dalvik_method.h>
#include <dalvik/dalvik_tokens.h>
#include <dalvik/dalvik_label.h>
#include <dalvik/dalvik_exception.h>
#include <debug.h>

#ifdef PARSER_COUNT
int dalvik_method_count = 0;
#endif

dalvik_method_t* dalvik_method_from_sexp(sexpression_t* sexp, const char* class_path,const char* file)
{

#ifdef PARSER_COUNT
    dalvik_method_count ++;
#endif

    dalvik_method_t* method = NULL;

    if(SEXP_NIL == sexp) return NULL;
    
    if(NULL == class_path) class_path = "(undefined)";
    if(NULL == file) file = "(undefined)";

    const char* name;
    sexpression_t *attrs, *arglist, *ret, *body;
    /* matches (method (attribute-list) method-name (arg-list) return-type body) */
    if(!sexp_match(sexp, "(L=C?L?C?_?A", DALVIK_TOKEN_METHOD, &attrs, &name, &arglist, &ret, &body))
        return NULL;

    /* get attributes */
    int attrnum;
    if((attrnum = dalvik_attrs_from_sexp(attrs)) < 0)
        return NULL;

    /* get number of arguments */
    int num_args;
    num_args = sexp_length(arglist);

    /* Now we know the size we have to allocate for this method */
    method = (dalvik_method_t*)malloc(sizeof(dalvik_method_t) + sizeof(dalvik_type_t*) * num_args);
    if(NULL == method) return NULL;

    method->num_args = num_args;
    method->path = class_path;
    method->file = file;
    method->name = name;

    /* Setup the type of argument list */
    int i;
    for(i = 0;arglist != SEXP_NIL; i ++)
    {
        sexpression_t *this_arg;
        if(!sexp_match(arglist, "(_?A", &this_arg, &arglist))
        {
            LOG_ERROR("invalid argument list");
            goto ERR;
        }
        if(NULL == (method->args_type[i] = dalvik_type_from_sexp(this_arg)))
        {
            LOG_ERROR("invalid argument type @ #%d", i);
            goto ERR;
        }

    }

    /* Setup the return type */
    if(NULL == (method->return_type = dalvik_type_from_sexp(ret)))
    {
        LOG_ERROR("invalid return type");
        goto ERR;
    }

    /* Now fetch the body */
    
    //TODO: process other parts of a method
    int current_line_number;    /* Current Line Number */
    dalvik_instruction_t *last = NULL;
    int last_label = -1;
    int from_label[DALVIK_MAX_CATCH_BLOCK];    /* NOTICE: the maximum number of catch block is limited to this constant */
    int to_label  [DALVIK_MAX_CATCH_BLOCK];
    int label_st  [DALVIK_MAX_CATCH_BLOCK];    /* 0: haven't seen any label related to the label. 
                                                * 1: seen from label before
                                                * 2: seen from and to label
                                                */
    dalvik_exception_handler_t* excepthandler[DALVIK_MAX_CATCH_BLOCK] = {};
    dalvik_exception_handler_set_t* current_ehset = NULL;
    int number_of_exception_handler = 0;
    for(;body != SEXP_NIL;)
    {
        sexpression_t *this_smt;
        if(!sexp_match(body, "(C?A", &this_smt, &body))
        {
            LOG_ERROR("invalid method body");
            goto ERR;
        }
        /* First check if the statement is a psuedo-instruction */
        const char* arg;
        char buf[40906];
        static int counter = 0;
        LOG_DEBUG("#%d current instruction : %s",(++counter) ,sexp_to_string(this_smt, buf) );
        if(sexp_match(this_smt, "(L=L=L?", DALVIK_TOKEN_LIMIT, DALVIK_TOKEN_REGISTERS, &arg))
        {
            /* (limit registers ...) */
            /* simplely ignore */
        }
        else if(sexp_match(this_smt, "(L=L?", DALVIK_TOKEN_LINE, &arg))
        {
            /* (line arg) */
            current_line_number = atoi(arg);
        }
        else if(sexp_match(this_smt, "(L=L?", DALVIK_TOKEN_LABEL, &arg))
        {
            /* (label arg) */
            int lid = dalvik_label_get_label_id(arg);
            int i;
            if(lid == -1) 
            {
                LOG_ERROR("can not create label for %s", arg);
                goto ERR;
            }
            last_label = lid;
            int enbaled_count = 0;
            dalvik_exception_handler_t* exceptionset[DALVIK_MAX_CATCH_BLOCK];
            for(i = 0; i < number_of_exception_handler; i ++)
            {
                if(lid == from_label[i] && label_st[i] == 0)
                    label_st[i] = 1;
                else if(lid == to_label[i] && label_st[i] == 1)
                    label_st[i] = 2;
                else if(lid == from_label[i] && label_st[i] != 0)
                    LOG_WARNING("meet from label twice, it might be a mistake");
                else if(lid == to_label[i] && label_st[i] != 1)
                    LOG_WARNING("to label is before from label, it might be a mistake");
                
                if(label_st[i] == 1)
                    exceptionset[enbaled_count++] = excepthandler[i];
            }
            current_ehset = dalvik_exception_new_handler_set(enbaled_count, excepthandler);
        }
<<<<<<< HEAD
=======
        else if(sexp_match(this_smt, "(L=A", DALVIK_TOKEN_ANNOTATION, &arg))
            /* Simplely ignore */;
        else if(sexp_match(this_smt, "(L=L=A", DALVIK_TOKEN_DATA, DALVIK_TOKEN_ARRAY, &arg))
            /* TODO: what is (data-array ....)statement currently ignored */;
        else if(sexp_match(this_smt, "(L=A", DALVIK_TOKEN_CATCH, &arg) || 
                sexp_match(this_smt, "(L=A", DALVIK_TOKEN_CATCHALL, &arg))
        {
            excepthandler[number_of_exception_handler] = 
                dalvik_exception_handler_from_sexp(
                        this_smt, 
                        from_label + number_of_exception_handler, 
                        to_label + number_of_exception_handler);
            if(excepthandler[number_of_exception_handler] == NULL)
            {
                LOG_WARNING("invalid exception handler %s", sexp_to_string(this_smt, NULL));
                continue;
            }
            label_st[number_of_exception_handler] = 0;
            number_of_exception_handler ++;
        }
        else if(sexp_match(this_smt, "(L=A", DALVIK_TOKEN_FILL, &arg))
        {
            //TODO: fill-array-data psuedo-instruction
        }
>>>>>>> fix memory leak
        else
        {
            dalvik_instruction_t* inst = dalvik_instruction_new();
            if(NULL == inst) goto ERR;
            if(dalvik_instruction_from_sexp(this_smt, inst, current_line_number, file) < 0)
            {
                LOG_ERROR("can not recognize instuction %s", sexp_to_string(this_smt, NULL));
                goto ERR;
            }
            if(NULL == last) method->entry = inst;
            else
            {
                last->next = inst;
                last = inst;
            }
            inst->handler_set = current_ehset; 
            if(last_label >= 0)
            {
                dalvik_label_jump_table[last_label] = inst;
                last_label = -1;
            }
        }
    }
    return method;
ERR:
    dalvik_method_free(method);
    return NULL;
}

void dalvik_method_free(dalvik_method_t* method)
{
    if(NULL == method) return;
    if(NULL != method->return_type) dalvik_type_free(method->return_type);
    int i;
    for(i = 0; i < method->num_args; i ++)
        if(NULL != method->args_type[i])
            dalvik_type_free(method->args_type[i]);
    free(method);
}