#pragma once

typedef long matrix2d_ele_t; /* you can use it by the way (void *)  */
typedef matrix2d_ele_t (*matrixed_ele_assign_t)(void *raw);
typedef matrix2d_ele_t (*metriax2d_calc_t)(matrix2d_ele_t left, matrix2d_ele_t right);

static inline matrix2d_ele_t matrix2d_calc_add(matrix2d_ele_t left, matrix2d_ele_t right)
{
    return left + right;
}

static inline matrix2d_ele_t matrix2d_calc_sub(matrix2d_ele_t left, matrix2d_ele_t right)
{
    return left - right;
}

static inline matrix2d_ele_t matrix2d_calc_mul(matrix2d_ele_t left, matrix2d_ele_t right)
{
    return left * right;
}

struct matrix2d_geometry
{
    unsigned int column;
    unsigned int line;
};

/* example of layout of field:  
*      filed[3][4] := { { 1, 2, 3, 4 }, { 5, 6, 7, 8 }, { 9, 10, 11, 12 } }
 * in matrix2d:
 *  -   1, 2, 3, 4  -
 *  |   5, 6, 7, 8  |
 *  _   9,10,11,12  _
 */
typedef struct matrix2d *matrix2d_pt;

extern void matrix2d_display(const matrix2d_pt m);

/* allocate or load buff as a matrix */
extern matrix2d_pt matrix2d_allocate(const void *raw, unsigned int raw_size, const struct matrix2d_geometry *geometry, const matrixed_ele_assign_t assignfn);

/* you will got a identity matrix like below when you specify scale==8 to invoke  @matrix2d_allocate_indentity
 * any matrix multiplied by the identity matrix is equal to itself
 * | 1,0,0,0,0,0,0,0 |
 * | 0,1,0,0,0,0,0,0 |
 * | 0,0,1,0,0,0,0,0 |
 * | 0,0,0,1,0,0,0,0 |
 * | 0,0,0,0,1,0,0,0 |
 * | 0,0,0,0,0,1,0,0 |
 * | 0,0,0,0,0,0,1,0 |
 * | 0,0,0,0,0,0,0,1 |
*/
extern matrix2d_pt matrix2d_allocate_identity(const unsigned int scale);
extern void matrix2d_free(matrix2d_pt m);

/* query element a specify location @geometry */
extern int matrix2d_get_element(const matrix2d_pt m, const struct matrix2d_geometry *geometry, matrix2d_ele_t *output);

/* of course, you can invoke @matrix2d_alloc_add to implement subtract method
 *
 *   | 1,2,3 |  +  | 7,8,9|  = | 1+7=8, 2+8=10,3+9=12 | = | 8, 10,12 |
 *   | 4,5,6 |     | 9,8,7|    | 4+9=13,5+8=13,6+7=13 |   | 13,13,13 |
 */
extern matrix2d_pt matrix2d_alloc_add(const matrix2d_pt left, const matrix2d_pt right, const metriax2d_calc_t addfn);

/*  count column of matrix A MUST equal to count of line in matrix B
 *
 *  | 1,2,3 |  x  | 7,8,9|  = | 1x7+2*1+3*4=21 1*8+2*2+3*5=27 1*9+2*3+3*6=33 | = | 21,27,33 |
 *  | 4,5,6 |     | 1,2,3|    | 4*7+5*1+6*4=57 4*8+5*2+6*5=72 4*9+5*3+6*6=87 |   | 57,72,87 |
 *                | 4,5,6|
 */
extern matrix2d_pt matrix2d_alloc_mul(const matrix2d_pt left, const matrix2d_pt right, const metriax2d_calc_t mulfn, const metriax2d_calc_t addfn);

/* transport line and column element order like below
 *
 * | 2,3,4 |   =>  | 2,5,6,7,1,2 |
 * | 5,3,5 |       | 3,3,2,9,4,5 |
 * | 6,2,3 |       | 4,5,3,9,7,9 |
 * | 7,9,9 |
 * | 1,4,7 |
 * | 2,5,9 |
*/
extern matrix2d_pt matrixed_alloc_transport(const matrix2d_pt m);
