#include "matrix2d.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct matrix2d
{
    unsigned int column;
    unsigned int line;
    matrix2d_ele_t field[0];
};

void matrix2d_display(const matrix2d_pt m)
{
    unsigned int i, ele_count;

    if (!m) {
        return;
    }
    
    ele_count = m->line * m->column;

    for (i = 0; i < ele_count; i++) {
        printf("%ld    ", m->field[i]);

        if ((i > 0) && ((i + 1) % m->column == 0)) {
            printf("\n");
        }
    }

    printf("--------------------------------------\n");
}

matrix2d_pt matrix2d_alloc(unsigned int line, unsigned int column)
{
    matrix2d_pt m;
    unsigned int ele_count;

    if (0 == column || 0 == line) {
        return NULL;
    }

    ele_count = line * column;
    if (NULL == (m = (matrix2d_pt )malloc(sizeof(*m) + sizeof(matrix2d_ele_t) * ele_count))) {
        return NULL;
    }
    m->column = column;
    m->line = line;
    memset(m->field, 0, sizeof(matrix2d_ele_t) * ele_count);
    return m;
}

void matrix2d_free(matrix2d_pt m)
{
    if (!m) {
        return;
    }
    free(m);
}

void *matrix2d_raw(matrix2d_pt m)
{
    if (!m) {
        return NULL;
    }

    if (m->column == 0 || m->line == 0) {
        return NULL;
    }

    return &m->field[0];
}

void matrix2d_iterate_element(const matrix2d_pt m, const matrixed_iterator_fp iterator, void *args)
{
    unsigned int i, ele_count;

    if (m && iterator) {
        ele_count = m->column * m->line;
        for (i = 0; i < ele_count; i++) {
            iterator(&m->field[i], i, args);
        }
    }
}

void matrix2d_iterate_line(const matrix2d_pt m, unsigned int line, const matrixed_iterator_fp iterator, void *args)
{
    unsigned int i, loc;

    if (m && iterator && line < m->line) {
        for (i = 0; i < m->column; i++) {
            loc = m->column * line + i;
            iterator(&m->field[loc], loc, args);
        }
    }
}

void matrix2d_iterate_column(const matrix2d_pt m, unsigned int column, const matrixed_iterator_fp iterator, void *args)
{
    unsigned int i, loc;

    if (m && iterator && column < m->column) {
        for (i = 0; i < m->line; i++) {
            loc = i * m->column + column;
            iterator(&m->field[loc], loc, args);
        }
    }
}

int matrix2d_query_element(const matrix2d_pt m, unsigned int line, unsigned int column, matrix2d_ele_t *output)
{
    if (!m) {
        return -1;
    }

    if (column >= m->column || line >= m->line) {
        return -1;
    }

    if (output) {
        *output = m->field[line * m->column + column];
    }
    return 0;
}

matrix2d_pt matrix2d_add(const matrix2d_pt left, const matrix2d_pt right, const metriax2d_calc_fp addfn)
{
    matrix2d_pt m;
    unsigned int ele_count, i;

    if (!left || !right) {
        return NULL;
    }

    if (left->column != right->column || left->line != right->line) {
        return NULL;
    }

    if (NULL == (m = matrix2d_alloc(left->line, left->column))) {
        return NULL;
    }

    ele_count = m->line * m->column;
    for (i = 0; i < ele_count; i++) {
        m->field[i] = ((NULL == addfn) ? matrix2d_calc_add(left->field[i], right->field[i]) : addfn(left->field[i], right->field[i]));
    }

    return m;
}

matrix2d_pt matrix2d_mul(const matrix2d_pt left, const matrix2d_pt right, const metriax2d_calc_fp mulfn, const metriax2d_calc_fp addfn)
{
    matrix2d_pt m;
    unsigned int i, j, idx_line, idx_column, ele_count;
    matrix2d_ele_t ele_left, ele_right, middle;

    if (!left || !right) {
        return NULL;
    }

    if (left->column != right->line ) {
        return NULL;
    }

    if (NULL == (m = matrix2d_alloc(left->line,left->column))) {
        return NULL;
    }
    ele_count = m->line * m->column;

    for (i = 0; i < ele_count; i++) {
        for (j = 0; j < left->column; j++) {
            idx_line = matrix2d_index2line(m, i);
            idx_column = j;
            matrix2d_query_element(left, idx_line, idx_column, &ele_left);
            
            idx_line = j;
            idx_column = matrix2d_index2column(m, i);
            matrix2d_query_element(right, idx_line, idx_column, &ele_right);
            
            middle = ((NULL == mulfn) ? matrix2d_calc_mul(ele_left, ele_right) : mulfn(ele_left, ele_right));
            m->field[i] =  ((NULL == addfn) ? matrix2d_calc_add(m->field[i] , middle) : addfn(m->field[i] , middle));
        }
    }

    return m;
}
 
matrix2d_pt matrix2d_scalar_mul(const matrix2d_pt left, const matrix2d_ele_t scalar, const metriax2d_calc_fp mulfn)
{
    matrix2d_pt m;
    unsigned int i, ele_count;

    if (!left) {
        return NULL;
    }

    if (0 == left->line || 0 == left->column) {
        return NULL;
    }

    if (NULL == (m = matrix2d_alloc(left->line, left->column))) {
        return NULL;
    }
    ele_count = m->line * m->column;

    for (i = 0; i < ele_count; i++) {
        m->field[i] = left->field[i] * scalar;
    }
    return m;
}

matrix2d_pt matrixed_transport(const matrix2d_pt src)
{
    matrix2d_pt m;
    unsigned int i, j, ele_count, off;

    if (!src) {
        return NULL;
    }

    if (NULL == (m = matrix2d_alloc(src->line, src->column))) {
        return NULL;
    }
    ele_count = m->line * m->column;

    off = 0;
    for (i = 0; i < src->column; i++) {
        for (j = 0; j < src->line; j++) {
            m->field[off++] = src->field[j * src->column + i];
        }
    }

    return m;
}

matrix2d_pt matrix2d_make_identity(const unsigned int scale)
{
    matrix2d_pt m;
    unsigned int ele_count, i;

    if (0 == scale ) {
        return NULL;
    }

    if ( NULL == (m = matrix2d_alloc(scale, scale))) {
        return NULL;
    }
    ele_count = scale * scale;

    for (i = 0; i < scale; i++) {
        m->field[i * scale + i] = 1;
    }
    return m;
}
