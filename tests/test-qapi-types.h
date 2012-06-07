/* AUTOMATICALLY GENERATED, DO NOT MODIFY */

/*
 * schema-defined QAPI types
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef TEST_QAPI_TYPES_H
#define TEST_QAPI_TYPES_H

#include "qapi/qapi-types-core.h"

extern const char *EnumOne_lookup[];
typedef enum EnumOne
{
    ENUM_ONE_VALUE1 = 0,
    ENUM_ONE_VALUE2 = 1,
    ENUM_ONE_VALUE3 = 2,
    ENUM_ONE_MAX = 3,
} EnumOne;

typedef struct NestedEnumsOne NestedEnumsOne;

typedef struct NestedEnumsOneList
{
    NestedEnumsOne *value;
    struct NestedEnumsOneList *next;
} NestedEnumsOneList;

typedef struct UserDefOne UserDefOne;

typedef struct UserDefOneList
{
    UserDefOne *value;
    struct UserDefOneList *next;
} UserDefOneList;

typedef struct UserDefTwo UserDefTwo;

typedef struct UserDefTwoList
{
    UserDefTwo *value;
    struct UserDefTwoList *next;
} UserDefTwoList;

typedef struct UserDefNested UserDefNested;

typedef struct UserDefNestedList
{
    UserDefNested *value;
    struct UserDefNestedList *next;
} UserDefNestedList;

typedef struct UserDefA UserDefA;

typedef struct UserDefAList
{
    UserDefA *value;
    struct UserDefAList *next;
} UserDefAList;

typedef struct UserDefB UserDefB;

typedef struct UserDefBList
{
    UserDefB *value;
    struct UserDefBList *next;
} UserDefBList;

typedef struct UserDefUnion UserDefUnion;

typedef struct UserDefUnionList
{
    UserDefUnion *value;
    struct UserDefUnionList *next;
} UserDefUnionList;

extern const char *UserDefUnionKind_lookup[];
typedef enum UserDefUnionKind
{
    USER_DEF_UNION_KIND_A = 0,
    USER_DEF_UNION_KIND_B = 1,
    USER_DEF_UNION_KIND_MAX = 2,
} UserDefUnionKind;

struct NestedEnumsOne
{
    EnumOne enum1;
    bool has_enum2;
    EnumOne enum2;
    EnumOne enum3;
    bool has_enum4;
    EnumOne enum4;
};

void qapi_free_NestedEnumsOneList(NestedEnumsOneList * obj);
void qapi_free_NestedEnumsOne(NestedEnumsOne * obj);

struct UserDefOne
{
    int64_t integer;
    char * string;
    bool has_enum1;
    EnumOne enum1;
};

void qapi_free_UserDefOneList(UserDefOneList * obj);
void qapi_free_UserDefOne(UserDefOne * obj);

struct UserDefTwo
{
    char * string;
    struct 
    {
        char * string;
        struct 
        {
            UserDefOne * userdef;
            char * string;
        } dict;
        bool has_dict2;
        struct 
        {
            UserDefOne * userdef;
            char * string;
        } dict2;
    } dict;
};

void qapi_free_UserDefTwoList(UserDefTwoList * obj);
void qapi_free_UserDefTwo(UserDefTwo * obj);

struct UserDefNested
{
    char * string0;
    struct 
    {
        char * string1;
        struct 
        {
            UserDefOne * userdef1;
            char * string2;
        } dict2;
        bool has_dict3;
        struct 
        {
            UserDefOne * userdef2;
            char * string3;
        } dict3;
    } dict1;
};

void qapi_free_UserDefNestedList(UserDefNestedList * obj);
void qapi_free_UserDefNested(UserDefNested * obj);

struct UserDefA
{
    bool boolean;
};

void qapi_free_UserDefAList(UserDefAList * obj);
void qapi_free_UserDefA(UserDefA * obj);

struct UserDefB
{
    int64_t integer;
};

void qapi_free_UserDefBList(UserDefBList * obj);
void qapi_free_UserDefB(UserDefB * obj);

struct UserDefUnion
{
    UserDefUnionKind kind;
    union {
        void *data;
        UserDefA * a;
        UserDefB * b;
    };
};
void qapi_free_UserDefUnionList(UserDefUnionList * obj);
void qapi_free_UserDefUnion(UserDefUnion * obj);

#endif
