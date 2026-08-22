#include "WKdm_internal.h"

/*
 * WK_pack_2bits()
 * Pack some multiple of four words holding two-bit tags (in the low
 * two bits of each byte) into an integral number of words, i.e.,
 * one fourth as many.
 * NOTE: Pad the input out with zeroes to a multiple of four words!
 */
static WK_word* WK_pack_2bits(WK_word* source_buf, WK_word* source_end, WK_word* dest_buf)
{
    register WK_word* src_next  = source_buf;
    WK_word*          dest_next = dest_buf;

    while (src_next < source_end) {
        register WK_word temp  = src_next[0];
        temp                  |= (src_next[1] << 2);
        temp                  |= (src_next[2] << 4);
        temp                  |= (src_next[3] << 6);

        dest_next[0] = temp;
        dest_next++;
        src_next += 4;
    }

    return dest_next;
}

/*
 * WK_pack_4bits()
 * Pack an even number of words holding 4-bit patterns in the low bits
 * of each byte into half as many words.
 * note: pad out the input with zeroes to an even number of words!
 */

static WK_word* WK_pack_4bits(WK_word* source_buf, WK_word* source_end, WK_word* dest_buf)
{
    register WK_word* src_next  = source_buf;
    WK_word*          dest_next = dest_buf;

    /* this loop should probably be unrolled */
    while (src_next < source_end) {
        register WK_word temp  = src_next[0];
        temp                  |= (src_next[1] << 4);

        dest_next[0] = temp;
        dest_next++;
        src_next += 2;
    }

    return dest_next;
}

/*
 * WK_pack_3_tenbits()
 * Pack a sequence of three ten bit items into one word.
 * note: pad out the input with zeroes to an even number of words!
 */
static WK_word* WK_pack_3_tenbits(WK_word* source_buf, WK_word* source_end, WK_word* dest_buf)
{
    register WK_word* src_next  = source_buf;
    WK_word*          dest_next = dest_buf;

    /* this loop should probably be unrolled */
    while (src_next < source_end) {
        register WK_word temp  = src_next[0];
        temp                  |= (src_next[1] << 10);
        temp                  |= (src_next[2] << 20);

        dest_next[0] = temp;
        dest_next++;
        src_next += 3;
    }

    return dest_next;
}

unsigned int WKdm_compress(WK_word* src_buf, WK_word* dest_buf, int byte_budget)
{
    unsigned          num_input_words = TARGET_PAGE_SIZE / BYTES_PER_WORD;
    DictionaryElement dictionary[DICTIONARY_SIZE];

    /*
     * arrays that hold output data in intermediate form during modeling
     * and whose contents are packed into the actual output after modeling
     */

    /*
     * sizes of these arrays should be increased if you want to compress
     * pages larger than 16KB
     */
    WK_word tempTagsArray[4096];    /* tags for everything          */
    WK_word tempQPosArray[4096];    /* queue positions for matches  */
    WK_word tempLowBitsArray[4096]; /* low bits for partial matches */

    /*
     * boundary_tmp will be used for keeping track of what's where in
     * the compressed page during packing
     */
    WK_word* boundary_tmp;

    /*
     * Fill pointers for filling intermediate arrays (of queue positions
     * and low bits) during encoding.
     * Full words go straight to the destination buffer area reserved
     * for them.  (Right after where the tags go.)
     */
    WK_word* next_full_patt;
    WK_word* start_next_full_patt;
    char*    next_tag            = (char*)tempTagsArray;
    char*    next_qp             = (char*)tempQPosArray;
    char*    start_next_qp       = next_qp;
    WK_word* next_low_bits       = tempLowBitsArray;
    WK_word* start_next_low_bits = next_low_bits;

    WK_word* next_input_word  = src_buf;
    WK_word* end_of_input     = src_buf + num_input_words;
    int      header_size      = (TAGS_AREA_OFFSET + TAGS_AREA_SIZE) * BYTES_PER_WORD;
    byte_budget              -= header_size;
    if (byte_budget <= 0) { return -1; }

    PRELOAD_DICTIONARY;

    next_full_patt       = dest_buf + TAGS_AREA_OFFSET + (num_input_words / 16);
    start_next_full_patt = next_full_patt;

    while (next_input_word < end_of_input) {
        WK_word* dict_location;
        WK_word  dict_word;
        WK_word  input_word = *next_input_word;

        /*
         * compute hash value, which is a byte offset into the dictionary,
         * and add it to the base address of the dictionary. Cast back and
         * forth to/from char * so no shifts are needed
         */
        dict_location = (WK_word*)((void*)(((char*)dictionary) + HASH_TO_DICT_BYTE_OFFSET(input_word)));

        dict_word = *dict_location;

        if (input_word == dict_word) { RECORD_EXACT(dict_location - dictionary); }
        else if (input_word == 0) {
            RECORD_ZERO;
        }
        else {
            WK_word input_high_bits = HIGH_BITS(input_word);
            if (input_high_bits == HIGH_BITS(dict_word)) {
                RECORD_PARTIAL(dict_location - dictionary, LOW_BITS(input_word));
                *dict_location = input_word;
            }
            else {
                byte_budget -= 4;
                if (byte_budget < 0) { return -1; }
                RECORD_MISS(input_word);
                *dict_location = input_word;
            }
        }
        next_input_word++;
    }

    if (byte_budget < 0) { return -1; }

    int miss    = next_full_patt - start_next_full_patt;
    int hits    = next_qp - start_next_qp;
    int partial = next_low_bits - start_next_low_bits;

    if (miss == 0 && hits == 0) {
        /* zero value page */
        return SV_RETURN;
    }

    if (partial == 0 && hits == (PAGE_SIZE_IN_WORDS - 1) && miss == 1 && (tempTagsArray[0] & 0xff) == 2) {
        /* same value page */
        return SV_RETURN;
    }

    if (partial == 1 && hits == PAGE_SIZE_IN_WORDS && (tempTagsArray[0] & 0xff) == 1) {
        /* same value page */
        return SV_RETURN;
    }
    int sparse_csize = (miss + hits) * 6 + 4;
    int normal_csize = 2 * partial / 3 + miss * 4 + hits / 2 + header_size;

    if (sparse_csize < normal_csize) {
        /* Mostly Zero */
        *(dest_buf++)          = MZV_MAGIC;
        unsigned short* output = (unsigned short*)dest_buf;
        next_input_word        = (WK_word*)src_buf;
        while (next_input_word < end_of_input) {
            WK_word input_word = *next_input_word;
            if (input_word != 0) {
                *(uint32_t*)(output += 2) = input_word;
                *(output++)               = (char*)next_input_word - (char*)src_buf;
            }
            next_input_word++;
        }
        return sparse_csize;
    }

    /*
     * Record (into the header) where we stopped writing full words,
     * which is where we will pack the queue positions.  (Recall
     * that we wrote the full words directly into the dest buffer
     * during modeling.
     */

    SET_QPOS_AREA_START(dest_buf, next_full_patt);

    /*
     * Pack the tags into the tags area, between the page header
     * and the full words area.  We don't pad for the packer
     * because we assume that the page size is a multiple of 16.
     */

    boundary_tmp = WK_pack_2bits(tempTagsArray, (WK_word*)((void*)next_tag), dest_buf + HEADER_SIZE_IN_WORDS);

    /* Pack the queue positions into the area just after
     * the full words.  We have to round up the source
     * region to a multiple of two words.
     */

    {
        unsigned int num_bytes_to_pack = (unsigned int)(next_qp - (char*)tempQPosArray);
        /* ceil((double) num_bytes_to_pack / 8) */
        unsigned int num_packed_words = (num_bytes_to_pack + 7) >> 3;
        unsigned int num_source_words = num_packed_words * 2;
        WK_word*     endQPosArray     = tempQPosArray + num_source_words;

        /* Pad out the array with zeros to avoid corrupting real packed values.
         */
        for (; /* next_qp is already set as desired */
             next_qp < (char*)endQPosArray; next_qp++)
        {
            *next_qp = 0;
        }

        boundary_tmp = WK_pack_4bits(tempQPosArray, endQPosArray, next_full_patt);
        /*
         * Record (into the header) where we stopped packing queue positions,
         * which is where we will start packing low bits.
         */
        SET_LOW_BITS_AREA_START(dest_buf, boundary_tmp);
    }

    /*
     * Pack the low bit patterns into the area just after
     * the queue positions.  We have to round up the source
     * region to a multiple of three words.
     */

    {
        unsigned int num_tenbits_to_pack = (unsigned int)(next_low_bits - tempLowBitsArray);
        /* ceil((double) num_tenbits_to_pack / 3) */
        unsigned int num_packed_words = (num_tenbits_to_pack + 2) / 3;
        unsigned int num_source_words = num_packed_words * 3;
        WK_word*     endLowBitsArray  = tempLowBitsArray + num_source_words;

        /* Pad out the array with zeros to avoid corrupting real packed values.
         */

        for (; /* next_low_bits is already set as desired */
             next_low_bits < endLowBitsArray; next_low_bits++)
        {
            *next_low_bits = 0;
        }

        boundary_tmp = WK_pack_3_tenbits(tempLowBitsArray, endLowBitsArray, boundary_tmp);

SET_LOW_BITS_AREA_END
(dest_buf, boundary_tmp);
}

return (unsigned int)((char*)boundary_tmp - (char*)dest_buf);
}
