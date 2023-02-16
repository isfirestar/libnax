#include "matrix2d.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define matrix2d_index2line(m, i)         ((i) / (m)->geometry.column)
#define matrix2d_index2column(m, i)       ((i) % (m)->geometry.column)

struct matrix2d
{
    matrix2d_ele_t *field;
    struct matrix2d_geometry geometry;
    unsigned int raw_size;
};

void matrix2d_display(const matrix2d_pt m)
{
    int i;
    int ele_count;

    if (!m) {
        return;
    }
    
    ele_count = m->geometry.line * m->geometry.column;

    for (i = 0; i < ele_count; i++) {
        printf("%ld    ", m->field[i]);

        if ((i > 0) && ((i + 1) % m->geometry.column == 0)) {
            printf("\n");
        }
    }

    printf("--------------------------------------\n");
}

matrix2d_pt matrix2d_allocate(const void *raw, unsigned int raw_size, const struct matrix2d_geometry *geometry, const matrixed_ele_assign_t assignfn)
{
    matrix2d_pt m;
    int i;
    unsigned int ele_count;
    unsigned int offset;

    if (0 == raw_size || !geometry || 0 == geometry->column || 0 == geometry->line) {
        return NULL;
    }


    m = (matrix2d_pt )malloc(sizeof(*m));
    if (!m) {
        return NULL;
    }
    m->geometry.column = geometry->column;
    m->geometry.line = geometry->line;
    m->raw_size = raw_size;
    ele_count = geometry->line * geometry->column;
    m->field = (matrix2d_ele_t *)malloc(sizeof(matrix2d_ele_t) * ele_count);
    if (!m->field) {
        free(m);
        return NULL;
    }

    if (raw && assignfn) {
        offset = 0;
        for (i = 0; i < ele_count; i++) {
            m->field[i] = assignfn( (unsigned char *)raw + offset);
            offset += raw_size;
        }
    } else {
        memset(m->field, 0, sizeof(matrix2d_ele_t) * ele_count);
    }

    return m;
}

matrix2d_pt matrix2d_allocate_identity(const unsigned int scale)
{
    matrix2d_pt m;
    unsigned int ele_count;
    int i;
    struct matrix2d_geometry geo;

    if (0 == scale ) {
        return NULL;
    }

    geo.line = geo.column = scale;
    if ( NULL == (m = matrix2d_allocate(NULL, sizeof(matrix2d_ele_t), &geo, NULL))) {
        return NULL;
    }
    ele_count = scale * scale;

    for (i = 0; i < scale; i++) {
        m->field[i * scale + i] = 1;
    }
    return m;
}

void matrix2d_free(matrix2d_pt m)
{
    if (!m) {
        return;
    }

    if (m->field) {
        free(m->field);
    }

    free(m);
}

matrix2d_pt matrix2d_alloc_add(const matrix2d_pt left, const matrix2d_pt right, const metriax2d_calc_t addfn)
{
    matrix2d_pt m;
    int i;
    unsigned int ele_count;

    if (!left || !right) {
        return NULL;
    }

    if (left->geometry.column != right->geometry.column || left->geometry.line != right->geometry.line) {
        return NULL;
    }

    m = matrix2d_allocate(NULL, left->raw_size, &left->geometry, NULL);
    if (!m) {
        return NULL;
    }

    ele_count = m->geometry.line * m->geometry.column;
    for (i = 0; i < ele_count; i++) {
        m->field[i] = ((NULL == addfn) ? matrix2d_calc_add(left->field[i], right->field[i]) : addfn(left->field[i], right->field[i]));
    }

    return m;
}

matrix2d_ele_t matrix2d_get_element(const matrix2d_pt m, const struct matrix2d_geometry *geometry)
{
    if (!m || !geometry) {
        return MATRIX2D_INVALID_ELEMENT;
    }

    if (geometry->column >= m->geometry.column || geometry->line >= m->geometry.line) {
        return MATRIX2D_INVALID_ELEMENT;
    }

    return m->field[geometry->line * m->geometry.column + geometry->column];
}

matrix2d_pt matrix2d_alloc_mul(const matrix2d_pt left, const matrix2d_pt right, const metriax2d_calc_t mulfn, const metriax2d_calc_t addfn)
{
    matrix2d_pt m;
    int i, j;
    unsigned int ele_count;
    matrix2d_ele_t ele_left, ele_right, middle;
    struct matrix2d_geometry geo;
    int cur_line;

    if (!left || !right) {
        return NULL;
    }

    if (left->geometry.column != right->geometry.line ) {
        return NULL;
    }

    geo.line = left->geometry.line;
    geo.column = right->geometry.column;
    m = matrix2d_allocate(NULL, left->raw_size, &geo, NULL);
    if (!m) {
        return NULL;
    }
    ele_count = m->geometry.line * m->geometry.column;

    for (i = 0; i < ele_count; i++) {
        for (j = 0; j < left->geometry.column; j++) {
            geo.line = matrix2d_index2line(m, i);
            geo.column = j;
            ele_left = matrix2d_get_element(left, &geo);
            geo.line = j;
            geo.column = matrix2d_index2column(m, i);
            ele_right = matrix2d_get_element(right, &geo);
            middle = ((NULL == mulfn) ? matrix2d_calc_mul(ele_left, ele_right) : mulfn(ele_left, ele_right));
            m->field[i] =  ((NULL == addfn) ? matrix2d_calc_add(m->field[i] , middle) : addfn(m->field[i] , middle));
        }
    }

    return m;
}
 
matrix2d_pt matrixed_alloc_transport(const matrix2d_pt src)
{
    matrix2d_pt m;
    struct matrix2d_geometry geo;
    int i, j;
    unsigned int ele_count;
    int off;

    if (!src) {
        return NULL;
    }

    geo.line = src->geometry.column;
    geo.column = src->geometry.line;
    m = matrix2d_allocate(NULL, src->raw_size, &geo, NULL);
    if (!m) {
        return NULL;
    }
    ele_count = m->geometry.line * m->geometry.column;

    off = 0;
    for (i = 0; i < src->geometry.column; i++) {
        for (j = 0; j < src->geometry.line; j++) {
            m->field[off++] = src->field[j * src->geometry.column + i];
        }
    }

    return m;
}
