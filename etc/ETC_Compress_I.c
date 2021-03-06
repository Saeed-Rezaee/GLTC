//
//  ETC_Compress_I.c
//  GLTC
//
//  Created by Michael Kwasnicki on 04.04.13.
//  Copyright (c) 2014 Michael Kwasnicki. All rights reserved.
//	This content is released under the MIT License.
//

#include "ETC_Compress_I.h"

#include "ETC_Compress_Common.h"
#include "ETC_Decompress.h"

#include <stdio.h>



static ETCUniformColorComposition_t ETC_UNIFORM_COLOR_LUT[ETC_TABLE_COUNT][ETC_PALETTE_SIZE][256];



static void buildBlock(ETCBlockColor_t *out_block, const rgb4_t in_C0, const rgb4_t in_C1, const int in_T0, const int in_T1, const bool in_FLIP, const uint8_t in_MODULATION[4][4]) {
    uint32_t bitField = generateBitField(in_MODULATION);

    ETCBlockI_t block;
    block.zero = 0;
    block.r0 = in_C0.r;
    block.g0 = in_C0.g;
    block.b0 = in_C0.b;
    block.r1 = in_C1.r;
    block.g1 = in_C1.g;
    block.b1 = in_C1.b;
    block.table0 = in_T0;
    block.table1 = in_T1;
    block.flip = in_FLIP;
    block.cBitField = bitField;
    out_block->b64 = block.b64;
}



static uint32_t uniformColor(rgb4_t *out_c, int *out_t, uint8_t out_modulation[2][4], const rgba8_t in_SUB_BLOCK_RGBA[2][4]) {
    rgba8_t col8, palette[4];
    rgb4_t col4 = RGB(0, 0, 0);
    uint32_t error = 0;
    uint32_t bestError = 0xFFFFFFFF;
    int bestT = 0;
    rgb4_t bestC = RGB(0, 0, 0);

    computeSubBlockCenter(&col8, in_SUB_BLOCK_RGBA);

    for (int t = 0; t < ETC_TABLE_COUNT; t++) {
        for (int p = 0; p < ETC_PALETTE_SIZE; p++) {
            error  = ETC_UNIFORM_COLOR_LUT[t][p][col8.r].error;
            error += ETC_UNIFORM_COLOR_LUT[t][p][col8.g].error;
            error += ETC_UNIFORM_COLOR_LUT[t][p][col8.b].error;
            col4.r = ETC_UNIFORM_COLOR_LUT[t][p][col8.r].c;
            col4.g = ETC_UNIFORM_COLOR_LUT[t][p][col8.g].c;
            col4.b = ETC_UNIFORM_COLOR_LUT[t][p][col8.b].c;

            if (error < bestError) {
                bestError = error;
                bestC = col4;
                bestT = t;
            }

            if (bestError == 0) {
                goto earlyExit;
            }
        }
    }

earlyExit:

    *out_c = bestC;
    *out_t = bestT;

    convert444to8888(&col8, bestC);
    computeRGBColorPaletteCommonID(palette, col8, bestT, ETC_MODIFIER_TABLE);
    return computeSubBlockError(out_modulation, in_SUB_BLOCK_RGBA, palette, true);
}



static uint32_t quick(rgb4_t *out_c, int *out_t, uint8_t out_modulation[2][4], const rgba8_t in_SUB_BLOCK_RGBA[2][4]) {
    rgb4_t col4;
    rgba8_t center, palette[4];
    int t;
    computeSubBlockMedian(&center, in_SUB_BLOCK_RGBA);
    computeSubBlockWidth(&t, in_SUB_BLOCK_RGBA);
    convert8888to444(&col4, center);
    convert444to8888(&center, col4);
    computeRGBColorPaletteCommonID(palette, center, t, ETC_MODIFIER_TABLE);

    *out_c = col4;
    *out_t = t;

    return computeSubBlockError(out_modulation, &in_SUB_BLOCK_RGBA[0], palette, true);
}



static uint32_t brute(rgb4_t *out_c, int *out_t, uint8_t out_modulation[2][4], const rgba8_t in_SUB_BLOCK_RGBA[2][4]) {
    rgba8_t col8, center, palette[4];
    rgb4_t col4, bestC;
    uint32_t error = 0xFFFFFFFF;
    uint32_t bestError = 0xFFFFFFFF;
    uint32_t bestDistance = 0xFFFFFFFF;
    int distance, dR, dG, dB;
    int t, bestT;
    int r, g, b;

    computeSubBlockCenter(&center, &in_SUB_BLOCK_RGBA[0]);

    for (t = 0; t < ETC_TABLE_COUNT; t++) {
        for (b = 0; b < 16; b++) {
            for (g = 0; g < 16; g++) {
                for (r = 0; r < 16; r++) {
                    col4 = (rgb4_t)RGB(r, g, b);
                    convert444to8888(&col8, col4);
                    computeRGBColorPaletteCommonID(palette, col8, t, ETC_MODIFIER_TABLE);

                    //                    for ( int i = 0; i < 4; i++ ) {
                    //                        printf( "%3d %3d %3d\n", palette[i].r, palette[i].g, palette[i].b );
                    //                    }
                    //
                    ////                    printf( "%d %2d %2d %2d\n", t, r, g, b );

                    error = computeSubBlockError(NULL, &in_SUB_BLOCK_RGBA[0], palette, true);
                    dR = center.r - col8.r;
                    dG = center.g - col8.g;
                    dB = center.b - col8.b;
                    distance = dR * dR + dG * dG + dB * dB;

                    if ((error < bestError) or ((error == bestError) and (distance < bestDistance))) {
                        bestC = col4;
                        bestT = t;
                        bestDistance = distance;
                        bestError = error;
                    }

                    if ((bestError == 0) and (bestDistance == 0)) {
                        goto earlyExit;
                    }
                }
            }
        }
    }

    //    exit( -1 );

earlyExit:

    *out_c = bestC;
    *out_t = bestT;

    convert444to8888(&col8, bestC);
    computeRGBColorPaletteCommonID(palette, col8, bestT, ETC_MODIFIER_TABLE);
    return computeSubBlockError(out_modulation, &in_SUB_BLOCK_RGBA[0], palette, true);
}


// computes the error for a 4x2 RGB sub-block
static uint32_t computeSubBlockErrorColorComponent(const rgba8_t in_SUB_BLOCK_RGBA[2][4], const rgba8_t in_PALETTE[4], const int in_COMPONENT) {
    uint32_t subBlockError = 0;
    uint32_t pixelError = 0;
    uint32_t lowestPixelError = 0;
    int32_t d;

    for (int sby = 0; sby < 2; sby++) {
        for (int sbx = 0; sbx < 4; sbx++) {
            rgba8_t pixel = in_SUB_BLOCK_RGBA[sby][sbx];
            lowestPixelError = 0xFFFFFFFF;

            for (int p = 0; p < 4; p++) {
                d = pixel.array[in_COMPONENT] - in_PALETTE[p].array[in_COMPONENT];
                pixelError = d * d;

                if (pixelError < lowestPixelError) {
                    lowestPixelError = pixelError;

                    if (lowestPixelError == 0) {
                        break;
                    }
                }
            }

            subBlockError += lowestPixelError;
        }
    }

    return subBlockError;
}



static uint32_t almostBrute(rgb4_t *out_c, int *out_t, uint8_t out_modulation[2][4], const rgba8_t in_SUB_BLOCK_RGBA[2][4]) {
    rgba8_t col8, center, palette[4];
    rgb4_t col4, bestC;
    uint32_t error = 0xFFFFFFFF;
    uint32_t bestError = 0xFFFFFFFF;
    //uint32_t bestDistance = 0xFFFFFFFF;
    //int distance, dR, dG, dB;
    int t, bestT;
    int r, g, b;

    computeSubBlockCenter(&center, &in_SUB_BLOCK_RGBA[0]);

    for (int sby = 0; sby < 2; sby++) {
        for (int sbx = 0; sbx < 4; sbx++) {
            rgba8_t pixel = in_SUB_BLOCK_RGBA[sby][sbx];
            printf("%3d %3d %3d %3d\n", pixel.r, pixel.g, pixel.b, pixel.a);
        }
    }


    for (t = 0; t < ETC_TABLE_COUNT; t++) {
        bestError = 0xFFFFFFFF;

        for (b = 0; b < 16; b++) {
            for (g = 0; g < 16; g++) {
                for (r = 0; r < 16; r++) {
                    col4 = (rgb4_t)RGB(r, g, b);
                    convert444to8888(&col8, col4);
                    computeRGBColorPaletteCommonID(palette, col8, t, ETC_MODIFIER_TABLE);

                    //                    for ( int i = 0; i < 4; i++ ) {
                    //                        printf( "%3d %3d %3d\n", palette[i].r, palette[i].g, palette[i].b );
                    //                    }
                    //
                    ////                    printf( "%d %2d %2d %2d\n", t, r, g, b );

                    error = computeSubBlockError(NULL, &in_SUB_BLOCK_RGBA[0], palette, true);
                    //                    dR = center.r - col8.r;
                    //                    dG = center.g - col8.g;
                    //                    dB = center.b - col8.b;
                    //                    distance = dR * dR + dG * dG + dB * dB;

                    if (error < bestError) {
                        bestC = col4;
                        bestT = t;
                        bestError = error;
                    }

                    //                    if ( ( bestError == 0 ) and ( bestDistance == 0) )
                    //                        goto earlyExit;
                }
            }
        }

        printf("%d: %2d %2d %2d\n", t, bestC.r, bestC.g, bestC.b);
        convert444to8888(&col8, bestC);
        computeRGBColorPaletteCommonID(palette, col8, t, ETC_MODIFIER_TABLE);

        for (int i = 0; i < 4; i++) {
            printf("%3d %3d %3d\n", palette[i].r, palette[i].g, palette[i].b);
        }

        uint32_t bestErrorR = 0xFFFFFFFF;
        uint32_t bestErrorG = 0xFFFFFFFF;
        uint32_t bestErrorB = 0xFFFFFFFF;
        rgb4_t bestRGB;

        for (r = 0; r < 16; r++) {
            col4 = (rgb4_t)RGB(r, r, r);
            convert444to8888(&col8, col4);
            computeRGBColorPaletteCommonID(palette, col8, t, ETC_MODIFIER_TABLE);
            error = computeSubBlockErrorColorComponent(&in_SUB_BLOCK_RGBA[0], palette, 0);

            if (error < bestErrorR) {
                bestErrorR = error;
                bestRGB.r = r;
            }

            error = computeSubBlockErrorColorComponent(&in_SUB_BLOCK_RGBA[0], palette, 1);

            if (error < bestErrorG) {
                bestErrorG = error;
                bestRGB.g = r;
            }

            error = computeSubBlockErrorColorComponent(&in_SUB_BLOCK_RGBA[0], palette, 2);

            if (error < bestErrorB) {
                bestErrorB = error;
                bestRGB.b = r;
            }
        }

        printf("%d: %2d %2d %2d\n", t, bestRGB.r, bestRGB.g, bestRGB.b);

        convert444to8888(&col8, bestRGB);
        computeRGBColorPaletteCommonID(palette, col8, t, ETC_MODIFIER_TABLE);

        for (int i = 0; i < 4; i++) {
            printf("%3d %3d %3d\n", palette[i].r, palette[i].g, palette[i].b);
        }

        printf("%d: %d6  %6d  %6d  %6d\n", t, bestError, bestErrorR, bestErrorG, bestErrorB);

        col4 = (rgb4_t)RGB(7, 8, 13);
        convert444to8888(&col8, col4);
        computeRGBColorPaletteCommonID(palette, col8, t, ETC_MODIFIER_TABLE);
        error = computeSubBlockError(NULL, &in_SUB_BLOCK_RGBA[0], palette, true);

        printf("7 8 13: %d\n", error);

        col4 = (rgb4_t)RGB(8, 8, 13);
        convert444to8888(&col8, col4);
        computeRGBColorPaletteCommonID(palette, col8, t, ETC_MODIFIER_TABLE);
        error = computeSubBlockError(NULL, &in_SUB_BLOCK_RGBA[0], palette, true);

        printf("8 8 13: %d\n", error);
    }

    exit(-1);

earlyExit:

    *out_c = bestC;
    *out_t = bestT;

    convert444to8888(&col8, bestC);
    computeRGBColorPaletteCommonID(palette, col8, bestT, ETC_MODIFIER_TABLE);
    return computeSubBlockError(out_modulation, &in_SUB_BLOCK_RGBA[0], palette, true);
}



/*
static uint32_t modest( rgb4_t * out_c, int * out_t, uint8_t out_modulation[2][4], const rgb8_t in_SUB_BLOCK_RGB[2][4] ) {
	rgb8_t palette[4];
	rgb8_t center8, col8;
	rgb4_t center4, col4, bestC;
	uint32_t error = 0xFFFFFFFF;
	uint32_t bestError = 0xFFFFFFFF;
	uint32_t bestDistance = 0xFFFFFFFF;
	int distance, dR, dG, dB;
	int t, bestT;
	int r = 0, g = 0, b = 0;

	computeSubBlockCenter( &center8, &in_SUB_BLOCK_RGB[0] );
    convert888to444( &center4, center8 );

	for ( t = 0; t < ETC_TABLE_COUNT; t++ ) {
        for ( int rgb = -15; rgb < 16; rgb++ ) {
            for ( b = -1; b < 2; b++ ) {
                if ( center4.b + b + rgb < 0 ) continue;
                if ( center4.b + b + rgb > 15 ) break;

                for ( g = -1; g < 2; g++ ) {
                    if ( center4.g + g + rgb < 0 ) continue;
                    if ( center4.g + g + rgb > 15 ) break;

                    for ( r = -1; r < 2; r++ ) {
                        if ( center4.r + r + rgb < 0 ) continue;
                        if ( center4.r + r + rgb > 15 ) break;

                        col4 = (rgb4_t)RGB( center4.r + r + rgb, center4.g + g + rgb, center4.b + b + rgb );
                        convert444to888( &col8, col4 );
                        computeRGBColorPaletteCommonID( palette, col8, t, ETC_MODIFIER_TABLE );
                        error = computeSubBlockError( NULL, &in_SUB_BLOCK_RGB[0], palette );
                        dR = center8.r - col8.r;
                        dG = center8.g - col8.g;
                        dB = center8.b - col8.b;
                        distance = dR * dR + dG * dG + dB * dB;

                        if ( ( error < bestError ) or ( ( error == bestError ) and ( distance < bestDistance ) ) ) {
                            bestC = col4;
                            bestT = t;
                            bestDistance = distance;
                            bestError = error;
                        }

                        if ( ( bestError == 0 ) and ( bestDistance == 0) )
                            goto earlyExit;
                    }
                }
            }
        }
	}

earlyExit:
	*out_c = bestC;
	*out_t = bestT;

	convert444to888( &col8, bestC );
	computeRGBColorPaletteCommonID( palette, col8, bestT, ETC_MODIFIER_TABLE );
	return computeSubBlockError( out_modulation, &in_SUB_BLOCK_RGB[0], palette );
}
*/


#pragma mark - exposed interface


uint32_t compressI(ETCBlockColor_t *out_block, const rgba8_t in_BLOCK_RGBA[4][4], const Strategy_t in_STRATEGY, const bool UNUSED(in_OPAQUE)) {
    rgb4_t c[2], bestC[2];
    int t[2], bestT[2], bestFlip;
    rgba8_t blockRGBA[4][4];
    uint8_t modulation[4][4];
    uint8_t outModulation[4][4];
    uint32_t blockError = 0xFFFFFFFF;
    uint32_t bestBlockError = 0xFFFFFFFF;
    uint32_t subBlockError = 0xFFFFFFFF;

    //  flip          no flip
    // +---------+   +-----+-----+
    // | a e i m |   | a e | i m |
    // | b f j n |   | b f | j n |
    // +---------+   | c g | k o |
    // | c g k o |   | d h | l p |
    // | d h l p |   +-----+-----+
    // +---------+

    for (int flip = 0; flip < 2; flip++) {
        for (int by = 0; by < 4; by++) {
            for (int bx = 0; bx < 4; bx++) {
                blockRGBA[by][bx] = (flip) ? in_BLOCK_RGBA[by][bx] : in_BLOCK_RGBA[bx][by];
            }
        }

        blockError = 0;

        for (int sb = 0; sb < 2; sb++) {
            if (isUniformColorSubBlock(&blockRGBA[sb * 2])) {
                blockError += uniformColor(&c[sb], &t[sb], &modulation[sb * 2], (const rgba8_t(*)[4])&blockRGBA[sb * 2]);
            } else {
                switch (in_STRATEGY) {
                    case kFAST:
                        subBlockError = quick(&c[sb], &t[sb], &modulation[sb * 2], (const rgba8_t(*)[4])&blockRGBA[sb * 2]);
                        break;

                    case kBEST:
                        subBlockError = brute(&c[sb], &t[sb], &modulation[sb * 2], (const rgba8_t(*)[4])&blockRGBA[sb * 2]);
                        break;
                }

                //                subBlockError = almostBrute( &c[sb], &t[sb], &modulation[sb * 2], (const rgba8_t(*)[4])&blockRGBA[sb * 2] );
                //
                blockError += subBlockError;
            }
        }

        if (blockError < bestBlockError) {
            bestBlockError = blockError;
            bestC[0] = c[0];
            bestC[1] = c[1];
            bestT[0] = t[0];
            bestT[1] = t[1];
            bestFlip = flip;

            for (int by = 0; by < 4; by++) {
                for (int bx = 0; bx < 4; bx++) {
                    outModulation[by][bx] = (flip) ? modulation[by][bx] : modulation[bx][by];
                }
            }
        }
    }

    buildBlock(out_block, bestC[0], bestC[1], bestT[0], bestT[1], bestFlip, (const uint8_t(*)[4])outModulation);
    //
    //    printf( "%d\n", bestBlockError );
    return bestBlockError;
}



void computeUniformColorLUTI() {
    ETCUniformColorComposition_t tmp = { 0, 65535 };

    // set everything to undefined
    for (int t = 0; t < ETC_TABLE_COUNT; t++) {
        for (int p = 0; p < ETC_PALETTE_SIZE; p++) {
            for (int c = 0; c < 256; c++) {
                ETC_UNIFORM_COLOR_LUT[t][p][c] = tmp;
            }
        }
    }

    // compute all colors that can be constructed with 4bpp
    for (uint8_t col4 = 0; col4 < 16; col4++) {
        int col8 = extend4to8bits(col4);

        for (int t = 0; t < ETC_TABLE_COUNT; t++) {
            for (int p = 0; p < ETC_PALETTE_SIZE; p++) {
                int col = clampi(col8 + ETC_MODIFIER_TABLE[t][p], 0, 255);
                ETC_UNIFORM_COLOR_LUT[t][p][col] = (ETCUniformColorComposition_t) {
                    col4, 0
                };
            }
        }
    }

    // fill the gaps with the nearest color
    fillUniformColorPaletteLUTGaps(ETC_UNIFORM_COLOR_LUT);
}



void printInfoI(ETCBlockColor_t *in_BLOCK) {
    ETCBlockI_t *block = (ETCBlockI_t *)in_BLOCK;
    //printf( "%i %i\n", block->table0, block->table1 );
    printf("I: (%3i %3i %3i) (%3i %3i %3i) %i %i : %i\n", block->r0, block->g0, block->b0, block->r1, block->g1, block->b1, block->table0, block->table1, block->flip);
    //printf( "I: (%3i %3i %3i) (%3i %3i %3i)\n", extend4to8bits( block->r0 ), extend4to8bits( block->g0 ), extend4to8bits( block->b0 ), extend4to8bits( block->r1 ), extend4to8bits( block->g1 ), extend4to8bits( block->b1 ) );
}
