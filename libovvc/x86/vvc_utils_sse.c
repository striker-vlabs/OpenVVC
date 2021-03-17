#include <emmintrin.h>
#include <smmintrin.h>
#include <getopt.h>
#include <stdio.h>
#include <stddef.h>

typedef int16_t TCoeff;
typedef int16_t TMatrixCoeff;

extern const int16_t DCT_II_2[4];
extern const int16_t DCT_II_4[16];
extern const int16_t DCT_II_8[64];
extern const int16_t DCT_II_16[256];
extern const int16_t DCT_II_32[1024];
extern const int16_t DCT_II_64[4096];

extern const int16_t DCT_VIII_4[16];
extern const int16_t DCT_VIII_8[64];
extern const int16_t DCT_VIII_16[256];
extern const int16_t DCT_VIII_32[1024];

extern const int16_t DST_VII_4[16];
extern const int16_t DST_VII_8[64];
extern const int16_t DST_VII_16[256];
extern const int16_t DST_VII_32[1024];

#define SHIFTV 7
#define SHIFTH 10
#define SIZEV 32
#define SIZEH 2

void afficherVecteur4SSE128(__m128i cible)
{
    int32_t test[4];
    _mm_store_si128((__m128i *)test, cible);

//    printf(" ________ ________ ________ ________\n|        |        |        |        |\n");
    printf("| %06d | %06d | %06d | %06d |\n",  test[0], test[1], test[2], test[3]);
//    printf("|________|________|________|________|\n");
}

void afficherVecteur8SSE128(__m128i cible)
{
    int16_t test[8];
    _mm_store_si128((__m128i *)test, cible);

//    printf(" ________ ________ ________ ________ ________ ________ ________ ________\n|        |        |        |        |        |        |        |        |\n");
    printf("| %06d | %06d | %06d | %06d | %06d | %06d | %06d | %06d |\n", test[0], test[1], test[2], test[3], test[4], test[5], test[6], test[7]);
//    printf("|________|________|________|________|________|________|________|________|\n");
}

void afficherTableau2D(int v, int h, TCoeff * m){
    int i,j;
    for (i = 0; i < v; ++i) {
        for (j = 0; j < h; ++j) {
            printf("%i ",m[i*h+j]);
        }
        printf("\n");
    }
    printf("\n");
}

void inverse_sse2_B4(const TCoeff *src, TCoeff *dst, int src_stride, int shift, int line, const TMatrixCoeff* iT){
    __m128i x1, x2; //Contient le vecteur à transformer
    __m128i d, d2;	//Contient coefficient DCT ou DST
    __m128i vhi[2], vlo[2], vh[2], vl[2], result[line]; //Variables pour calculs (result[] il faudrait mettre line ou la taille max 32)

    int nbstore = line/2;

    d = _mm_load_si128((__m128i *)&(iT[0]));
    d2 = _mm_load_si128((__m128i *)&(iT[8])); //8 car [taille coef DCT ou DST 4] x [2 (le milieu de la hauteur)]

    for(int i = 0; i < line; ++i){
        //Boucle for inutile d'ici jusqu'avant _mm_add car que 2 itérations
        x1 = _mm_unpacklo_epi64(_mm_set1_epi16(src[i]),_mm_set1_epi16(src[src_stride+i]));
        x2 = _mm_unpacklo_epi64(_mm_set1_epi16(src[2*src_stride+i]),_mm_set1_epi16(src[3*src_stride+i]));

        vhi[0] = _mm_mulhi_epi16(x1, d);            // mul hi
        vlo[0] = _mm_mullo_epi16(x1, d);            // mul lo
        vhi[1] = _mm_mulhi_epi16(x2, d2);            // mul hi
        vlo[1] = _mm_mullo_epi16(x2, d2);            // mul lo
        vl[0] = _mm_unpacklo_epi16(vlo[0], vhi[0]);	//blue
        vh[0] = _mm_unpackhi_epi16(vlo[0], vhi[0]);     //purple
        vl[1] = _mm_unpacklo_epi16(vlo[1], vhi[1]);	//green
        vh[1] = _mm_unpackhi_epi16(vlo[1], vhi[1]);     //orange

        result[i] = _mm_add_epi32(_mm_add_epi32(vl[0],vh[0]),_mm_add_epi32(vl[1],vh[1]));

        result[i] = _mm_add_epi32(result[i], _mm_set1_epi32(1<<(shift-1)));

        result[i] = _mm_srai_epi32(result[i], shift);

        //afficherVecteur4SSE128(result[i]);
    }

    for(int i = 0; i<nbstore; i++){
        result[i] = _mm_packs_epi32(result[2*i], result[2*i+1]); //clip pour repasser en 16
        _mm_store_si128((__m128i *)&(dst[i*8]), result[i]);	//dst[i*8] car result contient 8 résultat
    }
}

static const int16_t DCT_II_2_sse[8] = {
    64,  64, 64,  64,
    64, -64, 64, -64
};

void inverse_sse2_B2(const TCoeff *src, TCoeff *dst, int src_stride, int shift, int line, const TMatrixCoeff* iT){
    __m128i x, x1, x2; //Contient le vecteur à transformer
    __m128i d;	//Contient coefficient DCT ou DST
    __m128i vhi, vlo, vh, vl, result[line/2]; //Variables pour calculs (result[] il faudrait mettre line/2 ou la taille max 32/2)

    int nbline = line/2; //Nombre de ligne à traiter (divisé par 2)
    int nbstore = nbline/2;
    d = _mm_load_si128((__m128i *)&(DCT_II_2_sse[0])); //Si je la stocke comme souhaité
//    d = _mm_set_epi64x(*(int64_t *)&(iT[0]),*(int64_t *)&(iT[0]));

    for(int i = 0; i < nbline; i++){
        //Même chose d'utiliser unpacklo et unpackhi ici
        x1 = _mm_unpacklo_epi32(_mm_set1_epi16(src[2*i]),_mm_set1_epi16(src[src_stride+2*i]));
        x2 = _mm_unpacklo_epi32(_mm_set1_epi16(src[2*i+1]),_mm_set1_epi16(src[src_stride+2*i+1]));

        x = _mm_unpacklo_epi32(x1, x2);
//        afficherVecteur8SSE128(x);

//        afficherVecteur8SSE128(d);
        vhi = _mm_mulhi_epi16(x, d);            // mul hi
        vlo = _mm_mullo_epi16(x, d);            // mul lo
        vl = _mm_unpacklo_epi16(vlo, vhi);
        vh = _mm_unpackhi_epi16(vlo, vhi);
//        afficherVecteur4SSE128(vl);
//        afficherVecteur4SSE128(vh);

        result[i] = _mm_add_epi32(_mm_add_epi32(vl,vh), _mm_set1_epi32(1<<(shift-1)));
        result[i] = _mm_srai_epi32(result[i], shift);
//        afficherVecteur4SSE128(result[i]);

//        afficherVecteur4SSE128(result[i]);
    }

//    printf("%d\n", nbstore);
    if(nbstore == 0){
//        afficherVecteur4SSE128(result[0]);
//        afficherVecteur4SSE128(result[1]);
        result[0] = _mm_packs_epi32(result[2 * 0], result[2 * 0 + 1]); //clip pour repasser en 16
//        afficherVecteur8SSE128(result[0]);
        _mm_storel_epi64((__m128i *) &(dst[0]), result[0]);
    }else {
        for (int i = 0; i < nbstore; i++) {
            //Ne fonctione pas pour src de taille 2*2 car tableau sur 64 bits et pas 128
            result[i] = _mm_packs_epi32(result[2 * i], result[2 * i + 1]); //clip pour repasser en 16
            _mm_store_si128((__m128i *) &(dst[i * 8]), result[i]); //dst[i*8] car result contient 8 résultat
        }
    }
}

void inverse_sse2_B8(const TCoeff *src, TCoeff *dst, int src_stride, int shift, int line, const TMatrixCoeff* iT){
    __m128i x[8]; //Contient le vecteur à transformer
    __m128i d[8];	//Contient coefficient DCT ou DST
    __m128i vhi[8], vlo[8], vh[8], vl[8], result[line][2]; //Variables pour calculs (result[][2] il faudrait mettre line ou la taille max 32)

    for(int i = 0; i < 8; ++i){
        d[i] = _mm_load_si128((__m128i *)&(iT[i*8]));
    }

    for(int i = 0; i < line; ++i){
        for(int j = 0; j < 8; ++j){
            x[j] = _mm_set1_epi16(src[j*src_stride+i]);

            vhi[j] = _mm_mulhi_epi16(x[j], d[j]);            // mul hi
            vlo[j] = _mm_mullo_epi16(x[j], d[j]);            // mul lo
            vl[j] = _mm_unpacklo_epi16(vlo[j], vhi[j]);
            vh[j] = _mm_unpackhi_epi16(vlo[j], vhi[j]);
        }
        result[i][0] = _mm_set1_epi32(0);
        result[i][1] = _mm_set1_epi32(0);
        for(int j = 0; j < 4; ++j){
            result[i][0] = _mm_add_epi32(result[i][0], _mm_add_epi32(vl[2*j],vl[2*j+1]));
            result[i][1] = _mm_add_epi32(result[i][1], _mm_add_epi32(vh[2*j],vh[2*j+1]));
        }
        //afficherVecteur4SSE128(result[i][0]);
        //afficherVecteur4SSE128(result[i][1]);
        result[i][0] = _mm_add_epi32(result[i][0], _mm_set1_epi32(1<<(shift-1)));
        result[i][1] = _mm_add_epi32(result[i][1], _mm_set1_epi32(1<<(shift-1)));
        result[i][0] = _mm_srai_epi32(result[i][0], shift);
        result[i][1] = _mm_srai_epi32(result[i][1], shift);

        //afficherVecteur4SSE128(result[i][0]);
        //afficherVecteur4SSE128(result[i][1]);
    }

    for(int i = 0; i < line; i++){
        result[i][0] = _mm_packs_epi32(result[i][0], result[i][1]); //clip pour repasser en 16
        _mm_store_si128((__m128i *)&(dst[i*8]), result[i][0]); //dst[i*8] car result contient 8 résultat
    }
}

void inverse_sse2_B16(const TCoeff *src, TCoeff *dst, int src_stride, int shift, int line, const TMatrixCoeff* iT){
    __m128i x[8]; //Contient le vecteur à transformer
    __m128i d[8];	//Contient coefficient DCT ou DST
    __m128i vhi[8], vlo[8], vh[16], vl[16], result[line][4]; //Variables pour calculs (result[][4] il faudrait mettre line ou la taille max 32)

    // int nbstore = line/2;

    for(int l = 0; l < 2; ++l){
        for(int i = 0; i < line; ++i){
            result[i][2*l] = _mm_set1_epi32(0);
            result[i][2*l+1] = _mm_set1_epi32(0);
        }
    }

    for(int l = 0; l < 2; ++l){ //l permet calculer "la partie basse" de la matrice de transformé DCT/DST
        for(int k = 0; k < 2; ++k){ //k permet calculer l'autre partie des multiplications (la partie de droite de la matrice de transformé DCT/DST)
            for(int i = 0; i < 8; ++i){
                d[i] = _mm_load_si128((__m128i *)&(iT[i*16+k*128+l*8]));
            }

            for(int i = 0; i < line; ++i){
                for(int j = 0; j < 8; ++j){
                    x[j] = _mm_set1_epi16(src[j*src_stride+i+k*8*src_stride]);

                    vhi[j] = _mm_mulhi_epi16(x[j], d[j]);            // mul hi
                    vlo[j] = _mm_mullo_epi16(x[j], d[j]);            // mul lo
                    vl[j+8*k] = _mm_unpacklo_epi16(vlo[j], vhi[j]);
                    vh[j+8*k] = _mm_unpackhi_epi16(vlo[j], vhi[j]);
                }
                for(int j = 0; j < 4; ++j){
                    //afficherVecteur4SSE128(_mm_add_epi32(vl[2*j+8*k],vl[2*j+1+8*k]));
                    //afficherVecteur4SSE128(_mm_add_epi32(vh[2*j+8*k],vh[2*j+1+8*k]));
                    result[i][2*l] = _mm_add_epi32(result[i][2*l], _mm_add_epi32(vl[2*j+8*k],vl[2*j+1+8*k]));
                    result[i][2*l+1] = _mm_add_epi32(result[i][2*l+1], _mm_add_epi32(vh[2*j+8*k],vh[2*j+1+8*k]));
                }
            }
        }
    }

    for(int l = 0; l < 2; ++l){
        for(int i = 0; i < line; ++i){
            //afficherVecteur4SSE128(result[i][0]);
            //afficherVecteur4SSE128(result[i][1]);
            result[i][2*l] = _mm_add_epi32(result[i][2*l], _mm_set1_epi32(1<<(shift-1)));
            result[i][2*l+1] = _mm_add_epi32(result[i][2*l+1], _mm_set1_epi32(1<<(shift-1)));
            result[i][2*l] = _mm_srai_epi32(result[i][2*l], shift);
            result[i][2*l+1] = _mm_srai_epi32(result[i][2*l+1], shift);

            //afficherVecteur4SSE128(result[i][2*l]);
            //afficherVecteur4SSE128(result[i][2*l+1]);
        }
    }

    for(int l = 0; l < 2; ++l){
        for(int i = 0; i < line; i++){
            result[i][l] = _mm_packs_epi32(result[i][2*l], result[i][2*l+1]); //clip pour repasser en 16
            _mm_store_si128((__m128i *)&(dst[i*16+l*8]), result[i][l]);
        }
    }
}

void inverse_sse2_B32(const TCoeff *src, TCoeff *dst, int src_stride, int shift, int line, const TMatrixCoeff* iT){
    __m128i x[8]; //Contient le vecteur à transformer
    __m128i d[8];	//Contient coefficient DCT ou DST
    __m128i vhi[8], vlo[8], vh[32], vl[32], result[line][8]; //Variables pour calculs  (result[][8] il faudrait mettre line ou la taille max 32)

    // int nbstore = line/2;

    for(int l = 0; l < 4; ++l){
        for(int i = 0; i < line; ++i){
            result[i][2*l] = _mm_set1_epi32(0);
            result[i][2*l+1] = _mm_set1_epi32(0);
        }
    }

    for(int l = 0; l < 4; ++l){ //l permet calculer "les parties basses" de la matrice de transformé DCT/DST
        for(int k = 0; k < 4; ++k){ //k permet calculer les autres parties des multiplications (la partie de droite de la matrice de transformé DCT/DST)
            for(int i = 0; i < 8; ++i){
                d[i] = _mm_load_si128((__m128i *)&(iT[i*32+k*256+l*8]));
            }

            for(int i = 0; i < line; ++i){
                for(int j = 0; j < 8; ++j){
                    x[j] = _mm_set1_epi16(src[j*src_stride+i+k*8*src_stride]);

                    vhi[j] = _mm_mulhi_epi16(x[j], d[j]);            // mul hi
                    vlo[j] = _mm_mullo_epi16(x[j], d[j]);            // mul lo
                    vl[j+8*k] = _mm_unpacklo_epi16(vlo[j], vhi[j]);
                    vh[j+8*k] = _mm_unpackhi_epi16(vlo[j], vhi[j]);
                }
                for(int j = 0; j < 4; ++j){
                    //afficherVecteur4SSE128(_mm_add_epi32(vl[2*j+8*k],vl[2*j+1+8*k]));
                    //afficherVecteur4SSE128(_mm_add_epi32(vh[2*j+8*k],vh[2*j+1+8*k]));
                    result[i][2*l] = _mm_add_epi32(result[i][2*l], _mm_add_epi32(vl[2*j+8*k],vl[2*j+1+8*k]));
                    result[i][2*l+1] = _mm_add_epi32(result[i][2*l+1], _mm_add_epi32(vh[2*j+8*k],vh[2*j+1+8*k]));
                }
            }
        }
    }

    for(int l = 0; l < 4; ++l){
        for(int i = 0; i < line; ++i){
            //afficherVecteur4SSE128(result[i][0]);
            //afficherVecteur4SSE128(result[i][1]);
            result[i][2*l] = _mm_add_epi32(result[i][2*l], _mm_set1_epi32(1<<(shift-1)));
            result[i][2*l+1] = _mm_add_epi32(result[i][2*l+1], _mm_set1_epi32(1<<(shift-1)));
            result[i][2*l] = _mm_srai_epi32(result[i][2*l], shift);
            result[i][2*l+1] = _mm_srai_epi32(result[i][2*l+1], shift);

            //afficherVecteur4SSE128(result[i][2*l]);
            //afficherVecteur4SSE128(result[i][2*l+1]);
        }
    }

    for(int l = 0; l < 4; ++l){
        for(int i = 0; i < line; i++){
            result[i][l] = _mm_packs_epi32(result[i][2*l], result[i][2*l+1]); //clip pour repasser en 16
            _mm_store_si128((__m128i *)&(dst[i*32+l*8]), result[i][l]);
        }
    }
}