/*
 * Context Management
 *
 * This file implements the generic management of the variant objects and other
 * library features.
 */

#include <assert.h>
#include <endian.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include "c-dvar.h"
#include "c-dvar-private.h"

/**
 * c_dvar_new() - XXX
 */
_public_ int c_dvar_new(CDVar **varp) {
        _cleanup_(c_dvar_freep) CDVar *var = NULL;

        var = malloc(sizeof(*var));
        if (!var)
                return -ENOMEM;

        var->data = NULL;
        var->n_data = 0;
        var->poison = 0;
        var->ro = false;
        var->big_endian = !!(__BYTE_ORDER == __BIG_ENDIAN);
        var->current = NULL;

        *varp = var;
        var = NULL;
        return 0;
}

/**
 * c_dvar_free() - XXX
 */
_public_ CDVar *c_dvar_free(CDVar *var) {
        if (!var)
                return NULL;

        c_dvar_reset(var);
        free(var);

        return NULL;
}

/**
 * c_dvar_reset() - reset variant to initial state
 * @var:                variant to reset
 *
 * This resets the given variant to its initial state. Any allocated data is
 * released. All pointers to external resources are cleared and any state is
 * reset to initial values.
 */
_public_ void c_dvar_reset(CDVar *var) {
        if (var->current) {
                for ( ; var->current > var->levels; --var->current)
                        if (var->current->allocated_parent_type)
                                free(var->current->parent_type);

                /* root-level type is always caller-owned */
                assert(!var->current->allocated_parent_type);
        }

        if (!var->ro)
                free(var->data);

        var->data = NULL;
        var->n_data = 0;
        var->poison = 0;
        var->ro = false;
        var->big_endian = !!(__BYTE_ORDER == __BIG_ENDIAN);
        var->current = NULL;
}

/**
 * c_dvar_is_big_endian() - XXX
 */
_public_ bool c_dvar_is_big_endian(CDVar *var) {
        return var->big_endian;
}

/**
 * c_dvar_get_poison() - XXX
 */
_public_ int c_dvar_get_poison(CDVar *var) {
        return var->poison;
}

/**
 * c_dvar_get_data() - XXX
 */
_public_ void c_dvar_get_data(CDVar *var, void **datap, size_t *n_datap) {
        if (datap)
                *datap = var->data;
        if (n_datap)
                *n_datap = var->n_data;
}

/**
 * c_dvar_get_root_type() - XXX
 */
_public_ const CDVarType *c_dvar_get_root_type(CDVar *var) {
        return var->current ? var->levels[0].parent_type : NULL;
}

/**
 * c_dvar_get_parent_type() - XXX
 */
_public_ const CDVarType *c_dvar_get_parent_type(CDVar *var) {
        return var->current ? var->current->parent_type : NULL;
}

/*
 * Internal helper that verifies the next varg-type to read is @c. This is
 * called as iterator on format-strings for c-dvar variant bulk readers and
 * writers. That is, @c is the next character in the format string. This
 * function then verifies it matches what is expected. Otherwise, an error is
 * returned.
 */
int c_dvar_next_varg(CDVar *var, char c) {
        char real_c;

        /*
         * Our varg format string language extends the type strings to become a
         * full streaming language. To verify they match the elements in the
         * actual type, we need the real type character to match against. We
         * store it in @real_c, based on what is given as @c.
         */
        switch (c) {
        case '[':
        case ']':
                /* array stream */
                real_c = 'a';
                break;
        case '<':
        case '>':
                /* variant stream */
                real_c = 'v';
                break;
        case ')':
                real_c = '(';
                break;
        case '}':
                real_c = '{';
                break;
        default:
                /* everything else matches exactly */
                real_c = c;
                break;
        }

        switch (c) {
        case ']':
        case '>':
        case ')':
        case '}':
                /*
                 * A container can only be exited if fully parsed. This means,
                 * the type iterator must be at the end and the container type
                 * must match.
                 * Note that for arrays the type-iterator is never advanced, so
                 * it is the job of the caller to verify additional constraints
                 * if required.
                 */
                if ((real_c != 'a' && var->current->n_type) ||
                    var->current->container != real_c)
                        return -ENOTRECOVERABLE;

                break;

        case 'a':
        case 'v':
                /*
                 * Arrays and variants must be read one-by-one, using the "[]"
                 * and "<>" operators. There is no atomic reader for an entire
                 * array or variant right now.
                 */
                return -ENOTRECOVERABLE;

        case '[':
        case '<':
        case '(':
        case '{':
                /*
                 * Entering a type must not exceed the maximum depth. The type
                 * parser verifies this already, but this can be circumvented
                 * by using nested variants or hand-crafted types.
                 */
                if (_unlikely_(var->current >= var->levels + C_DVAR_TYPE_DEPTH_MAX - 1))
                        return -ENOTRECOVERABLE;

                /* fallthrough */
        default:
                if (_unlikely_(!var->current->n_type ||
                               var->current->i_type->element != real_c))
                        return -ENOTRECOVERABLE;

                break;
        }

        return 0;
}
