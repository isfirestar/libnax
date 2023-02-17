#pragma once

#if defined __cplusplus
#define matrix2d_function extern "C"
#else
#define matrix2d_function extern
#endif

/* example of layout of field:  
*      filed[3][4] := { { 1, 2, 3, 4 }, { 5, 6, 7, 8 }, { 9, 10, 11, 12 } }
 * in matrix2d:
 *  -   1, 2, 3, 4  -
 *  |   5, 6, 7, 8  |
 *  _   9,10,11,12  _
 */
typedef struct matrix2d *matrix2d_pt;
typedef long matrix2d_ele_t; /* you can use it by the way (void *)  */
typedef unsigned int matrix2d_iterator_t;
typedef int matrix2d_boolean_t;
typedef void (*matrixed_iterator_fp)(matrix2d_ele_t *ele, unsigned int ele_index, void *args);
typedef matrix2d_ele_t (*metriax2d_calc_fp)(matrix2d_ele_t left, matrix2d_ele_t right);

#define matrix2d_index2line(m, i)           ((i) / *((unsigned int *)(m) + 0))
#define matrix2d_index2column(m, i)         ((i) % *((unsigned int *)(m) + 0))
#define matrix2d_number_of_line(m)          (*((unsigned int *)(m) + 1))
#define matrix2d_number_of_column(m)        (*((unsigned int *)(m) + 0))

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

matrix2d_function void matrix2d_display(const matrix2d_pt m);

/* assign and release method
 * after successful allocation, a standard null matrix in the meaning of methematic shall be returned
 * otherwise, return value shall be NULL */
matrix2d_function matrix2d_pt matrix2d_alloc(unsigned int line, unsigned int column);
matrix2d_function void matrix2d_free(matrix2d_pt m);

/* direct obtain the raw field pointer in matrix  */
matrix2d_function void *matrix2d_raw(matrix2d_pt m);
/* obtain the size in bytes of entire matrix data field */
matrix2d_function unsigned int matrix2d_size(const matrix2d_pt m);
/* test two matrix a idential equal */
matrix2d_function matrix2d_boolean_t matrix2d_equal(const matrix2d_pt left, const matrix2d_pt right);
/* query element value by specify geometry position in matrix @m */
matrix2d_function int matrix2d_query_element(const matrix2d_pt m, unsigned int line, unsigned int column, matrix2d_ele_t *output);
/* iterate matrix @m from 3 dimemsion */
matrix2d_function void matrix2d_iterate_element(const matrix2d_pt m, const matrixed_iterator_fp iterator, void *args);
matrix2d_function void matrix2d_iterate_line(const matrix2d_pt m, unsigned int line, const matrixed_iterator_fp iterator, void *args);
matrix2d_function void matrix2d_iterate_column(const matrix2d_pt m, unsigned int column, const matrixed_iterator_fp iterator, void *args);

/* of course, you can invoke @matrix2d_alloc_add to implement subtract method
 * calling thread have ability to redirecte addition method during the matrix calculation by specify @addfn
 *
 *   | 1,2,3 |  +  | 7,8,9|  = | 1+7=8, 2+8=10,3+9=12 | = | 8, 10,12 |
 *   | 4,5,6 |     | 9,8,7|    | 4+9=13,5+8=13,6+7=13 |   | 13,13,13 |
 */
matrix2d_function matrix2d_pt matrix2d_add(const matrix2d_pt left, const matrix2d_pt right, const metriax2d_calc_fp addfn);

/*  numer of column of matrix A MUST equal to numer of line of matrix B
 *  calling thread have ability to redirecte multiply and addition method during the matrix calculation by specify @mulfn and @addfn
 *
 *  | 1,2,3 |  x  | 7,8,9|  = | 1x7+2*1+3*4=21 1*8+2*2+3*5=27 1*9+2*3+3*6=33 | = | 21,27,33 |
 *  | 4,5,6 |     | 1,2,3|    | 4*7+5*1+6*4=57 4*8+5*2+6*5=72 4*9+5*3+6*6=87 |   | 57,72,87 |
 *                | 4,5,6|
 */
matrix2d_function matrix2d_pt matrix2d_mul(const matrix2d_pt left, const matrix2d_pt right, const metriax2d_calc_fp mulfn, const metriax2d_calc_fp addfn);
matrix2d_function matrix2d_pt matrix2d_scalar_mul(const matrix2d_pt m, const matrix2d_ele_t scalar, const metriax2d_calc_fp mulfn);

/* transport line and column element order like below
 *
 * | 2,3,4 |   =>  | 2,5,6,7,1,2 |
 * | 5,3,5 |       | 3,3,2,9,4,5 |
 * | 6,2,3 |       | 4,5,3,9,7,9 |
 * | 7,9,9 |
 * | 1,4,7 |
 * | 2,5,9 |
*/
matrix2d_function matrix2d_pt matrixed_transport(const matrix2d_pt m);

/* you will got a identity matrix like below when you specify scale==6 to invoke  @matrix2d_allocate_indentity
 * any matrix multiplied by the identity matrix is equal to itself
 *
 * | 1,0,0,0,0,0 |
 * | 0,1,0,0,0,0 |
 * | 0,0,1,0,0,0 |
 * | 0,0,0,1,0,0 |
 * | 0,0,0,0,1,0 |
 * | 0,0,0,0,0,1 |
*/
matrix2d_function matrix2d_pt matrix2d_make_identity(const unsigned int scale);
