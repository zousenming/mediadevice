#include "fct.h"
#include <polarssl/config.h>

#include <polarssl/gcm.h>

#ifdef _MSC_VER
#include <basetsd.h>
typedef UINT32 uint32_t;
#else
#include <inttypes.h>
#endif

/*
 * 32-bit integer manipulation macros (big endian)
 */
#ifndef GET_UINT32_BE
#define GET_UINT32_BE(n,b,i)                            \
{                                                       \
    (n) = ( (uint32_t) (b)[(i)    ] << 24 )             \
        | ( (uint32_t) (b)[(i) + 1] << 16 )             \
        | ( (uint32_t) (b)[(i) + 2] <<  8 )             \
        | ( (uint32_t) (b)[(i) + 3]       );            \
}
#endif

#ifndef PUT_UINT32_BE
#define PUT_UINT32_BE(n,b,i)                            \
{                                                       \
    (b)[(i)    ] = (unsigned char) ( (n) >> 24 );       \
    (b)[(i) + 1] = (unsigned char) ( (n) >> 16 );       \
    (b)[(i) + 2] = (unsigned char) ( (n) >>  8 );       \
    (b)[(i) + 3] = (unsigned char) ( (n)       );       \
}
#endif

int unhexify(unsigned char *obuf, const char *ibuf)
{
    unsigned char c, c2;
    int len = strlen(ibuf) / 2;
    assert(!(strlen(ibuf) %1)); // must be even number of bytes

    while (*ibuf != 0)
    {
        c = *ibuf++;
        if( c >= '0' && c <= '9' )
            c -= '0';
        else if( c >= 'a' && c <= 'f' )
            c -= 'a' - 10;
        else if( c >= 'A' && c <= 'F' )
            c -= 'A' - 10;
        else
            assert( 0 );

        c2 = *ibuf++;
        if( c2 >= '0' && c2 <= '9' )
            c2 -= '0';
        else if( c2 >= 'a' && c2 <= 'f' )
            c2 -= 'a' - 10;
        else if( c2 >= 'A' && c2 <= 'F' )
            c2 -= 'A' - 10;
        else
            assert( 0 );

        *obuf++ = ( c << 4 ) | c2;
    }

    return len;
}

void hexify(unsigned char *obuf, const unsigned char *ibuf, int len)
{
    unsigned char l, h;

    while (len != 0)
    {
        h = (*ibuf) / 16;
        l = (*ibuf) % 16;

        if( h < 10 )
            *obuf++ = '0' + h;
        else
            *obuf++ = 'a' + h - 10;

        if( l < 10 )
            *obuf++ = '0' + l;
        else
            *obuf++ = 'a' + l - 10;

        ++ibuf;
        len--;
    }
}

/**
 * This function just returns data from rand().
 * Although predictable and often similar on multiple
 * runs, this does not result in identical random on
 * each run. So do not use this if the results of a
 * test depend on the random data that is generated.
 *
 * rng_state shall be NULL.
 */
static int rnd_std_rand( void *rng_state, unsigned char *output, size_t len )
{
#if !defined(__OpenBSD__)
    size_t i;

    if( rng_state != NULL )
        rng_state  = NULL;

    for( i = 0; i < len; ++i )
        output[i] = rand();
#else
    if( rng_state != NULL )
        rng_state = NULL;

    arc4random_buf( output, len );
#endif /* !OpenBSD */

    return( 0 );
}

/**
 * This function only returns zeros
 *
 * rng_state shall be NULL.
 */
static int rnd_zero_rand( void *rng_state, unsigned char *output, size_t len )
{
    if( rng_state != NULL )
        rng_state  = NULL;

    memset( output, 0, len );

    return( 0 );
}

typedef struct
{
    unsigned char *buf;
    size_t length;
} rnd_buf_info;

/**
 * This function returns random based on a buffer it receives.
 *
 * rng_state shall be a pointer to a rnd_buf_info structure.
 * 
 * The number of bytes released from the buffer on each call to
 * the random function is specified by per_call. (Can be between
 * 1 and 4)
 *
 * After the buffer is empty it will return rand();
 */
static int rnd_buffer_rand( void *rng_state, unsigned char *output, size_t len )
{
    rnd_buf_info *info = (rnd_buf_info *) rng_state;
    size_t use_len;

    if( rng_state == NULL )
        return( rnd_std_rand( NULL, output, len ) );

    use_len = len;
    if( len > info->length )
        use_len = info->length;

    if( use_len )
    {
        memcpy( output, info->buf, use_len );
        info->buf += use_len;
        info->length -= use_len;
    }

    if( len - use_len > 0 )
        return( rnd_std_rand( NULL, output + use_len, len - use_len ) );

    return( 0 );
}

/**
 * Info structure for the pseudo random function
 *
 * Key should be set at the start to a test-unique value.
 * Do not forget endianness!
 * State( v0, v1 ) should be set to zero.
 */
typedef struct
{
    uint32_t key[16];
    uint32_t v0, v1;
} rnd_pseudo_info;

/**
 * This function returns random based on a pseudo random function.
 * This means the results should be identical on all systems.
 * Pseudo random is based on the XTEA encryption algorithm to
 * generate pseudorandom.
 *
 * rng_state shall be a pointer to a rnd_pseudo_info structure.
 */
static int rnd_pseudo_rand( void *rng_state, unsigned char *output, size_t len )
{
    rnd_pseudo_info *info = (rnd_pseudo_info *) rng_state;
    uint32_t i, *k, sum, delta=0x9E3779B9;
    unsigned char result[4], *out = output;

    if( rng_state == NULL )
        return( rnd_std_rand( NULL, output, len ) );

    k = info->key;

    while( len > 0 )
    {
        size_t use_len = ( len > 4 ) ? 4 : len;
        sum = 0;

        for( i = 0; i < 32; i++ )
        {
            info->v0 += (((info->v1 << 4) ^ (info->v1 >> 5)) + info->v1) ^ (sum + k[sum & 3]);
            sum += delta;
            info->v1 += (((info->v0 << 4) ^ (info->v0 >> 5)) + info->v0) ^ (sum + k[(sum>>11) & 3]);
        }

        PUT_UINT32_BE( info->v0, result, 0 );
        memcpy( out, result, use_len );
        len -= use_len;
        out += 4;
    }

    return( 0 );
}


FCT_BGN()
{
#ifdef POLARSSL_GCM_C


    FCT_SUITE_BGN(test_suite_gcm)
    {

        FCT_TEST_BGN(gcm_nist_validation_aes_12812800128_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "1014f74310d1718d1cc8f65f033aaf83" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "6bb54c9fd83c12f5ba76cc83f7650d2c" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "0b6b57db309eff920c8133b8691e0cac" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800128_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "d874a25f2269e352ccdd83cc2d4e45b7" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "9717abb9ed114f2760a067279c3821e3" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "0e09e53e5fe8d818c5397c51173eda97" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800128_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "7dab77e23b901c926454f29677eb62d4" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "8aaec11c4a0f053d7f40badd31a63e27" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "cec2e3230d8b762acee527e184e4c0db" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800120_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "2397f163a0cb50b0e8c85f909b96adc1" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "97a631f5f6fc928ffce32ee2c92f5e50" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "3b74cca7bcdc07c8f8d4818de714f2" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800120_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "a7adc0d3aacef42397bbca79dd65dbdf" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c6d3114c1429e37314683081d484c87c" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "d88141d27fe1748919845cfa5934bc" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800120_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "10171805d7f7a6d87b64bda57474d7fc" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "fad65b50c1007c4b0c83c7a6720cacb8" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "c3d3f240d3f3da317eae42a238bcc1" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800112_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "8aaa0c85d214c6c9e9e260e62f695827" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "84e25c916f38dd6fdb732c0d6d8f86bb" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "a774815a2a8432ca891ef4003125" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800112_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "def8b6a58b8e582e57700bab4f2a4109" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "3615439e9fb777439eb814256c894fb2" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "537be9c88d3a46845e6cf5f91e11" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800112_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "5894231d743f79638687c070b60beee1" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "e34cd13b897d1c9b8011a0e63950c099" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "d582c4bc083a8cf1af4d5c2c9b11" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800104_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "6b25f9cbdc3bcd27fd245a1c411594bc" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "a6526f8c803b69dd5f59feca1cff78e2" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "c7e19e08a09a9c1fa698202890" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800104_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "b3235422897b6459798a97ddd709db3d" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "96679e9362f919217d5e64068969d958" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "44ed41bda0eb0958d407b7b787" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800104_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "f65bc795434efba3c5399ed3c99ff045" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "2e727c19a89cba6f9c04d990245fceed" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "64830ed7f772e898800fc9ae2a" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280096_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "c6c66d50f2f76c4e911b3b17fcdcba1d" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "77b42158a4ef5dc33039d33631bb0161" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "1bce3ba33f73e750ab284d78" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280096_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "13558db9b7441c585d381ffc16b32517" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "addf5dbe0975c5ad321e14dd4bdc2ad2" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "f413c3bf125ce5317cd1c6bd" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280096_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "74638628b1361c2954ce0ac5456a1155" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c5861507c879e6864d7cb1f77cc55cc6" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "8a514fdc7835711e4f458199" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280064_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "7815d22c5c081df9ac2114aaa2c0cbf9" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "822f83cd9f249dfc204b5957f0b0deab" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "aa1f69f5d3bb79e5" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280064_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "1a847a47823cb9c298e4107c6aaff95c" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "39348f80c6bc489f9315be7a6fcbb96f" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "c3b3f31e56cf4895" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280064_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "16e67ea248ea6db08af1d810cb10574e" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "50386e2075eb15ca3f3e6db6bff01969" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "3d4f3b8526a376ae" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280032_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "26a8301636ba93e7f56309143f184241" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c7e32b1d312971bdc344aefaf45461bc" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "25f1b41c" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280032_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "130a07c467067148da2790f90d73ff32" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "800b81c9d2ff3a8e15690ffb4117e211" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "abcc8d71" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280032_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "ccfaae59c3196b8c403716424ea601f5" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "f9b059de0efa4e3f364763d63d098410" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "8933444f" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024128_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "b5beefbdd23360f2dd1e6e3c1ddbfebf" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "81a8494f85be635d71e5663789162494" );
            add_len = unhexify( add_str, "f9ebf242b616a42e2057ede3b56b4c27349fed148817a710654de75d1cfc5f6304709b46ef1e2ccb42f877c50f484f8a8c6b0a25cff61d9537c3fd0c69bbc6ef21cbec8986cbc9b6e87963b8d9db91b7134afe69d3d9dec3a76b6c645f9c5528968f27396cc9e989d589369c90bbfefb249e3fa416451bc3d6592cc5feefbd76" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "159a642185e0756d46f1db57af975fa3" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024128_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "c465aa8fe5d534c912e654f5aaed5857" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "5c155f7194b0d0a17b9a0c234d609443" );
            add_len = unhexify( add_str, "a3f8d705b233b574399f72350b256cb4893e130688913ce3def8e44687688c0352ff987aea35dc53bc95cdb9cdcc6e6eb280265d9a1af38d526392ab63c9b043c1b1b43e18321e84eb7e08884f2463c32b55eb5859fb10918595a724a61cfdf935e4f96d0721612720d46a946487b525779f6ce0abf04fc5608351119b7427d2" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "9595a6d879cd7a949fa08e95d2b76c69" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024128_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "744b9e1692d8974d7dec349ebd7fe1e8" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "62ad4b09fd554e0d6b3937839e693e5b" );
            add_len = unhexify( add_str, "6f9978f7078f0030c45caf49128ff72943a208a2398d08d132239f3ab5c184708e4222ec9ccde69dc86d1700c2fe0af939454bbb3962327158557860b6fa492ab8201df262a6209705c7e3129419bce8b827320893c1579ca05b32c81b3963b849428f71fe7528e710557a272117199163a35ebfbaba78f7676f7e566b16311a" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "634f6fe9625be8b1af9f46bcc0fa3162" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024120_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "097c059535037c6b358dbb5a68b5f2b1" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "00caedfa078c27e3d9551e3fb8d98d77" );
            add_len = unhexify( add_str, "6c4bde11129a959fcd6a482cb19f5f1c582c042b314f7997b0450242f9e669dc1cbb0a3b7a185bf8b035267e6f03206268008e2b97864d44d6a9c6b1b4b067d623c4b4e9c608042ea9120aed3bee80886352683891496d8980e40b8480c98c2fe08f945aa1ef6007c65220319dd8678184ab54e81083b746ec6441e87a568e0c" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "5075ef45c6326726264703f72badde" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024120_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "d25db5eca46c16490294423ca0c35660" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "6f37f15d6c7ea816278ab977c29fa45e" );
            add_len = unhexify( add_str, "bd76fd431cea72a288e5d7289c651c93b5f429a54f85249021d6b595eb9ce26e18914a381a6b0299acc3725431b352670f206b731be718a598ec123dce0a2c5ac0aa4641b092e704da9f967b909ca55c2722298365a50dcb5b5ec03a1d0cbb67b8de1e8b06e724af91137e0d98e7dc1e8253887da453cdcbd2eca03deacaabb8" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "00510851e9682213d4124d5517ebaf" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024120_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "b3c6258a726aff94a7bcc41646c68157" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "7f5b3315afe5167a7e9061ab8b005588" );
            add_len = unhexify( add_str, "0ef3384862c7e00c2912e7fde91345dc3134b5448e6838f41135ba9199c03a7f208887e467563b39a6c1316540c1401e8ff148386c50fcf15724a65d3210b17832d63cdce76bd2b458348332b0b542122a57e381475a59440f280db6e1f4b8d0babfd47e3db11a9ef89cba5f334f0e8e72be30afb2b1ef2df8eb7f8d3da033c4" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "180489039ccf4a86c5f6349fc2235b" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024112_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "73cd0a1e2b6e12fbaa7cbace77d5119c" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "d897681764bcc3b62c26b4aaf407cefa" );
            add_len = unhexify( add_str, "8c773e14a906c7deae362d1bf3d7e54c6be4c74c691b7f2d248693b2619219fba6eb5bc45f77af1cf7c05d3dd463158f884fe82290d145135889fd851b86ee282aa20bbdf6af78c7f9db6128b8b99e7f9b270fd222efa18f7aca6932a1024efb72113e812b3f9d2d4ccc7c85f5898ddacccbf1b441cd74097740dd922b57bade" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "d8811a8990191f1e5bd15be84995" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024112_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "c1dfddafe076d0ceebb0f37bb25bc0b1" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "29c56db10cea802c19fb6230227ab2bf" );
            add_len = unhexify( add_str, "287b73cdc62ce058cdceff8e9af7afc321716f69da9eef60c2de93630ba7d0ed0a9d303cd15521a2647159b8478593f3dd3f5b7c52081e5154e55ccbff371d7e5dfc2d05e14d666a01ec2cc6028aacadfd78dfc73bf639fc4dfa0a0c46415902bbda2443620fa5e0ce4fccf1b8591e3a548f95755102a8438300753ea5f61b9f" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "309fedad1f3b81e51d69e4162e6f" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024112_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "2c4087ccd28ceda147d2fcfc18579b1e" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "9cbdd67c79ab46bcbcfa96fa2c3d7e87" );
            add_len = unhexify( add_str, "35088d18dff0a9d3929ce087668aae1d364b37a97102f3f43e11950e6ec8296d0c99b00cd1c5dff53d3a38475e7da7b9ee4ce0c6388a95d3f8b036414e4b79cd02b5468cbb277f930e7c92432a609db1effe65f60f1174b58f713e199491f9e0c29ba1f2e43306775d18c1136274af61488a2f932e95eceadfe3fe4b854fe899" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "b7e83207eb313b3ceb2360bc8d4f" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024104_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "bb66584c8b18f44c11f3bd7180b9b11d" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "39c82aee03ce0862ff99f8812cdbdcf0" );
            add_len = unhexify( add_str, "45ec858e0a5c6d81144ba893e0002818a70e9a19002a5471993077241b3fcfb4fd984f2450803293882d1c7ecb654e611578fe7d258f9a2ca3b5f0c0f0d0ec4828bdeb9299914ff2ac4cc997cf54fa908afdb3eae9f91d67c4637e1f9eb1eae2b3f482ddd5467668bc368b96bbbfc33b9ae2658e4ca43fcf4b66ba2a079d65f1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "24332fd35a83b1dfb75969819b" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024104_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "7b2a230c8978d4e38fa5096ddc19d6f5" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "cd25e744a78af858e825e1fd070324ee" );
            add_len = unhexify( add_str, "628baac336862573cee158cd3935c34df3055dadc9c1695e9ea18724f6457f0d1833aab30b85a99e0793e56000de5d6d5cb2327a4cc8bec40cd198459e7b93617713e63bbd15381a066bc44a69c9ad3dfb1984f8b33a9429eda3068d3ac5fbbaaee2b952a486e58d674ffca641d9ec1d102600af11641fd5fff725204e6c34a8" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "68d49d495ff092ca8e5a2c16cb" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024104_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "73aa576e1dfad2c993afcc088bd8d62b" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "712e665a0a83e8ecad97e92afeb35706" );
            add_len = unhexify( add_str, "314e5fee776e9d5d2a1fb64ceb78e2c9a560a34724e30da860b5588fe63d50838cb480ff8ac61d7958b470b1bfd4c84799af6cb74c4a331b198204a251e731f7d785b966da595b745d01769623492c18b9dd8bd3c75249effd2032658c715906a71dbbed847027ea75d647f9803296a41906e0915250854597a163035a8d3f45" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "a41f5c9c7de2694c75856460d4" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102496_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "83f7631c4d4c466c9246cbc48e2dde6f" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "f5d6c8c252cb687a931c38f58f74943c" );
            add_len = unhexify( add_str, "1f35e94a35d0f424bf690a15038126a41502593612efe6333cf94ea0565ca6acdefae8d74dae62df95e9261c6596c3397220e044c5b08cf39cccb27315d9b795da321204910274a93436bc0573fdba04ae6bb14c6ca955cf8b9e193a12e05796d7f4b397507614dabc457f1cd3ce19e439b6e62703f2189372938b29b7a542b9" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "bb85dbd858ab7b752da7e53c" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102496_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "784e023b2d4c978151d05ee71533c56c" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "f16d041b9f0f454db9985c8558ef8a61" );
            add_len = unhexify( add_str, "91f6e108c294640c7bc65d102d3d25a7bfbbe114acec9b495636689afd65fff794837946602ef04de7d4304a81809e0f7ddc45c476c29fd5286fcf4dd1ba76ed3ce88abdb51cd21e7aaeecb13238ac031da87ab96b2a13157278bf669d0efae28852ec3585d520d54502881322f7977d03954e17e7c0c0d8f762e34f59ca141e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "59699c639d67be6a6d7c9789" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102496_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "d3a2ec66e4a72cb3540e87f4e67c7e58" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "07a9cf9f44b07e3067d60e276322e9fb" );
            add_len = unhexify( add_str, "d7e722b82e8607a64fbfeefc7887009298f06a637fe937277e3a76e8addaeeb460ba0743912c07b500b4b51e9fec2b7eddf691d155baf689f75968160c19a8330e254220142ae843bf0687aabeb74ab607227b0a7539ec3cfea72a5c35f236623af78beffaee6e7b1adc2895732ffedb3f8520710f04eb9c2ce9b2cae215ed5c" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "f29aec72368bfcfa9ae815fd" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102464_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "83f382a90146544ef4871bde891aed22" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c6f664f5ccfd1aaefb60f7fa3b642302" );
            add_len = unhexify( add_str, "656a2f221a1339d8f5c26393a08fa31859f626eec9a68afb6ee30e5b6859d1cbb5ed7dea6cbc4a5d537d70227d0608185df71a0252fa313be4d804567c162b743814f8b8306155931fdecf13822a524868b99a27fd2ff8f98c16edccd64520e2dce1ad645fd5255c7c436d9b876f592ef468397b00857ba948edf21215d63d99" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "09df79dd8b476f69" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102464_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "64334f10a62c26fef79d9024d4ba7c5f" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "7b85251554d4f0ff89980cf3568c5caa" );
            add_len = unhexify( add_str, "dab2892262a1832a473cd3481acbd3d1820f14361c275514ec693b40f2170ea5ff82c4f7e95a7c783ea52c43a0a399c37b31319a122fd1a722e6631efa33f8bfb6dc193986580f0344d28842a3a4a5ca6880552557f3915a65501f6ee0c1b68a4c9040f0fac381cbccb6a6e9bca23b99f2ef1abbca71c69aa27af2db176bf37d" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "3e8406900a4c28bc" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102464_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "1c98ca4971c3a6333c18b88addf13368" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "7f617f08e826a3c61882c3e00c203d4b" );
            add_len = unhexify( add_str, "ab1531fce0f279d21091c3334bd20afa55c7155bfc275330ed45f91cfc953771cbde2582f4be279918ac8b9ae07cb3b2efd14292e094891d4841be329678ad58d714fc8ce4bffe51f539f4240c14ba883b95cdc32cf4a9fd6ba4ffeafa0d6718989c46483c96cfca3fe91000f9f923d7f96725e966de068b5da65546fe38f70e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "58cc756d3bf9b6f9" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102432_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "247d3abeb807bde959e68b40a3750045" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "3f5390cd7921fcb42c59f0db05a8a62f" );
            add_len = unhexify( add_str, "81abf375da7157a1a56068d0918037fecb7296d9b1771c54ae6030abda4b9d76feff818de81747980b2c1b005e36b3be36afbf1092edef6fd875d2903d73612addf206a6ae65886421059c70990a6ee33197f92bed649901fed62fdd20c30d81baf6090f50d9f59290528e58a0b7412ace0a293369f2b4c8d72c2fb0e1c432f5" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "37bb4857" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102432_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "622be8cd3c757de00fbb7ab4563ce14f" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "16c53a843b1549716d7c06b141861862" );
            add_len = unhexify( add_str, "a15d101580d549f2401bf0f36be0f83724875205c9109d2d69d2609cbf67504b918f0859303192b4075f952454f3e7152f898f997b36afc0356712fc08db3343054b20e88ad1274e019bf8fcc3c921d3bc8f9c1d1d24adc61f6033a83ef46a84762304f1903553748b13b1647c96eb8702ebb41ccea4d9cfebcb177c453277f2" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "35778596" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102432_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "8a660aa0191f9816261387d5aeb262f6" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c720cb31e841480da5ba656e9b93f066" );
            add_len = unhexify( add_str, "d979affe395bd048db26d26908a1c2a435905299086cc55bb65ef782f5aed99c41743c3ae252ea087f5453bdc605abd784b337b60960946358da2218b076826659a1fafa59124a00a3424fce0d00c38eea85cfb3d1e01bcb09d9870d5b3fe728f394e0e512f5aa849d0550d45a7cc384f1e4c6b2e138efbc8f586b5b5ed09212" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "cf7944b1" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240128_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "ce0f8cfe9d64c4f4c045d11b97c2d918" );
            pt_len = unhexify( src_str, "dfff250d380f363880963b42d6913c1ba11e8edf7c4ab8b76d79ccbaac628f548ee542f48728a9a2620a0d69339c8291e8d398440d740e310908cdee7c273cc91275ce7271ba12f69237998b07b789b3993aaac8dc4ec1914432a30f5172f79ea0539bd1f70b36d437e5170bc63039a5280816c05e1e41760b58e35696cebd55" );
            iv_len = unhexify( iv_str, "ad4c3627a494fc628316dc03faf81db8" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "0de73d9702d9357c9e8619b7944e40732ac2f4dd3f1b42d8d7f36acb1f1497990d0ec3d626082cdb1384ec72a4c1d98955ba2a3aae6d81b24e9ce533eb5ede7210ae4a06d43f750138b8914d754d43bce416fee799cc4dd03949acedc34def7d6bde6ba41a4cf03d209689a3ad181f1b6dcf76ca25c87eb1c7459cc9f95ddc57" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "5f6a3620e59fe8977286f502d0da7517" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240128_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "81371acd5553fdadc6af96fdeee4c64d" );
            pt_len = unhexify( src_str, "940806fd5ddcab9937b4ba875e46bb4b7e9688d616d17fd24646f1ef1457819f55887f53bd70039bb83b4d346aabe805288ab7a5756874bdc2b3d4894217d3a036da5e9e162fa2d9819ceb561ecf817efc9493b9a60796f6dc5e717ac99bc4ba298eee4f3cd56bbc07dde970d4f07bbfa1f5fe18c29a3927abe11369091df28f" );
            iv_len = unhexify( iv_str, "3262501ed230bc4f5a190ab050e1bcee" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "ffeb1907bdbfea877890a6e972a533ae661a903a257b3b912c7c768cc988e05afd71a9e6117d90d1e1b54f55de9b10cbce7a109452567483cc8d6a68b9e56da10802630591fdd8d55f9e172f0f58a7e0c56a73a1ae3c3062f0997b364eb0885d48e039b2ba1bd14dbb9c74a41cbd4b52564e470d1a8038d15207a7650bd3f1d6" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "227d422f8797b58aa6a189658b770da9" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240128_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "ef5295e9ae74729e222df6dab251158d" );
            pt_len = unhexify( src_str, "59372848432f86f5740500391d2e5d5fbe1f80ea876a0ecb9a5b298d9ea7cdc28620aeb2fda015345ae476f265351b2c6b6fcd66bc8aae4dc8a95c1350cda204da3d2d2fc5e6e142dc448296d5df0cc349d1eba2fa98d2f468662616274a147fbe07927440afa3967ac09a03a8de0b03f3036bde5e272e3c4c5ff169dd730238" );
            iv_len = unhexify( iv_str, "194d08fcc3c08ab96fa724c381274d3f" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "fdceeffdc8390bde6b910544db61db2f345eba0664f78f65d94b90e3e2a5251be374b3c5d881460cfff3549a01f84eb9d54087306a20f5156cd555e46bd2173386c90ea47983320fcbf24e09a05f2ec4b2577287d05e050b55b3002b753de49abef895ee97015810c06d09212b0c09e4910c64ac3981795a1e360197740360fd" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "e94603dbd8af99ab1e14c602a38a0328" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240120_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "26db035f2ddd9f5672c6f6af156838d7" );
            pt_len = unhexify( src_str, "92c315936847649756b0b1bb4a3453e6e6da866f8088d96da44412d9f47a22dda0cd817287ba42163be59a69f73963059139fb3ba44bc5ebfd95b6742546dfb4fe95608dca71911d1347be68179d99c9ebf7ee1d56b17195f8794f3a658d7cad2317ed1d4bc246cd4530e17147e9ecdf41091a411a98bb6047eee8b4f1e4a9ef" );
            iv_len = unhexify( iv_str, "3686d49bb8c7bd15546d453fdf30e1f3" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "1ac98e9ccfe63a2f12a011e514f446c4c0e22dd93613b1b9b8f56d148be8a24e3682dfc1cde2b69e72d200b516a99e7466dae8cc678c6117dc14b2364cd2b952aed59722056d7dae4cfdb7d9c4f716aef2aa91a4f161d01c98d92d974247bb972de0557e175177ce34361be40c30ab9ac46240016e5ad350c3b7232c5920e051" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "b744316880b0df3d4f90c3ffa44144" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240120_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "d5c63757197a132cbb33351fd2d81a46" );
            pt_len = unhexify( src_str, "e970b62ce5f06b15f8448aa2a095c2b3c8adf535e110e7f374411ed51fa19f9c4926045f796b7cd8a942b6a19811b7aae59fce37e50d6ca5a4a57bfb041a5b51c1ee82b54d03be22d9dc2bb9a2e708503b85e2479b0425a033ae825b4f232ca373e280e3cc97cf0d79397a81fb30d3b41cdaa3e788470cde86734e10a58b1e3a" );
            iv_len = unhexify( iv_str, "a669a4d2f841f9a0b9ede1fb61fee911" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "522ba7220d0d4bea7ab9ca74ad8fa96ba337f7aa749cd26186499081ba325df6d6b90a81bd1c7adda0cd1ca065894f14a074ec13eff117b2a00042038aea55850056a63adf04f58fcd7269085f5ad1ef17ce7b6c40804127f14747a2ad93ec31fada83663af025a3b90c20a4ae415b1c960094e5fd57db0d93a81edcce64f72d" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "7bfce3c8e513a89a5ee1480db9441f" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240120_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "f380d3bf0d55a1cd56b7e78359eb6c66" );
            pt_len = unhexify( src_str, "c0e977e91c1c50ee78d4a56c527b2d31a1a14f261aa77e52d910f8f230de4908b5cc6943e28b8c6e7ac61eebe270dcfde48d140ec13792371932e545b6ef4b52d1dfdf54c60ff892b74095a3f4a2b9000acd2cac04666a2305343b8c09f89dcc0c25bbe2a39b14624118df025962edec3dfc58d36fcac531b291ec45b5159e22" );
            iv_len = unhexify( iv_str, "ba3300f3a01e07dde1708343f01304d4" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "752f09b518616a91a802cf181532c7ec65b54c59c1bab3860f0ad19971a9e5bc8843524c5ffac827067b462ebb328e2eff4dd931728de882055129997204e78717becd66e1f6c9e8a273c4251896343604ac289eb1880207a8ea012626e18e69ad7573ef73071b8e2fb22c75c7fc7bf22382d55a5d709c15e4e8ff14e2bf81e4" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "fbf8818aee5c71ebfd19b0bcd96a7a" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240112_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "47c807cd1cf181040a4e3b1d94659db8" );
            pt_len = unhexify( src_str, "c4a52c1f1f0d32c21fb85fba21d1b358b332efa066c7893c566b2e859efdde99fc67bb6167cdb0485a8ed53dd1068d90bc990f360b044039791be6048ba0ee4ce1090c9fce602af59d69069f5bff8b6219aaaed5a9b1bfc8c5b7250c5a6cfe86586fa8064124d551da38d429a17696eb1a7a0341c363f010eafd26683eecdf82" );
            iv_len = unhexify( iv_str, "9963a3fb156beacd6dd88c15e83929df" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "e784ab006de8a52de1d04bc2c680d847c5decdd777cb2475ad4ab1dc529882d9e51cff5451b14ea5ff9a9bab5c5474e8a331d79564acdb2ac8159e0f46e9019bf80650c481fdaf1680cadcb8c5de9f924760b376ce5736cc4970cb8715b5999f577436283a4c21469306840af36d1e069616157d1b9ce75de3adb13d201cdf1b" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "51e8ce23f415a39be5991a7a925b" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240112_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "a0b033d14fe902aa0892b0e87f966c41" );
            pt_len = unhexify( src_str, "1cc751d890cd102486d81c618c23fa335067ac324ef11f7eddc937853db6e16d0f73727725a5a5bd580705416ecd97e368464ed0aea923ffb71c23c37f9cf9c8bd81cdbdc3d0ac34a875db3167ec1d519004d4fa4bba041af67af1ed3d4e09c32b3e8e10abd91f46836cec74b1f9c5b06c05f3b18caa78e7ff185db212b52ce0" );
            iv_len = unhexify( iv_str, "ad4dee18e6c19433ad52021164f8afb7" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "a30044582dacf57332b04402e993831df0a4c1364a83c9bce7353979fb444cd1b3fe747e2c933457ff21f39e943a38a85457bfe99dc09af886734d6e4218fc65138055ad8eb5d3044f4eed658e312b6165199e682ffa226558dc4b516f8d519f149bb5a40d2bb7d59ece9e5fd05358c89e635792ad20c73c174719f9b28c7358" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "6a18a4f880ce9e6796e1086ed05b" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240112_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "c4030ca84f132bfabaf660e036f56377" );
            pt_len = unhexify( src_str, "a8fe98e2b4880d12c99c9d5193b3537b3fbc5165cc1327395174d989be5741f867332271cdc52ddb295ddbeba33698073054c6d2416fafaeb0a76aad870a6fb6097a29fba99f858d49418572c8e4dc0d074ca8af7727c773c8617495b1195d6b2687a2e37fad116dd721b60bcb5471d548c6dafe3ecdcf0c962e4659a61f4df3" );
            iv_len = unhexify( iv_str, "975df9c932a46d54d677af8a6c9c9cc3" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "86b20fecebc4cf88a6a382d693117cd2a3c9eab747bf5df5f1d35e341d204d8fea6694b92552e347da676bc8d3353984e96472a509f5208ce100a2a9232478417947f85f10993c9d6939c8138bd6151aef8e2038536e8ba1ba84442e27586c1b642f9505455c738e9fd2c1b2527d1ecd3a2f6ed6e3869000ef68417ec99ff7a2" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "3516909124c0c1f9c30453c90052" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240104_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "6e210de363f170a7ccb1b9cec8d34737" );
            pt_len = unhexify( src_str, "89853fa002985a45651f2a7db2b45b7e7a7d33ce6c438ec4533c7fa257e1a384130369a68184a807fd0d92a70d91d7ddc56e5c5172c872257230d7aeb9293d785b1b8835dcde753798caff4abcd8bbc5378cd505dcf904aa69902e4f38699be972099adffc8778bd844a9a03e6b58a721a73324d956f20f2ffd00d3491f72f42" );
            iv_len = unhexify( iv_str, "39fe20b051ba21319a745349d908c4bf" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "ac9d74f8f405fd482287a4a7fa359caca095c0f1b46744f19c3c11e13b0c605b9857c8cc5a1754b95bcc658416f463bf8764f373205941885948259916eaabd964f2d6c2d784f928dc5eefe331f6c04b4862d4c8e966530de6bf533a10818de852de3af7f521b167cb4eb7141ba8ae8a17be1eb714fd26a474bbbbe870a659dc" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "7a2dfc88ad34d889f5e344ee0e" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240104_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "6bbfeda23ea644fb37666b05dc47f590" );
            pt_len = unhexify( src_str, "a85ec4c2c160deda7e3de0ae449eea6ed1d24e2c8f3d5151f2ac0fd869f5a763981733b68f46c5197d76c26cce7ddc8afc6cdf4536d771cf3e9cef0098e270c5e1ff72cb0ad7f84abf44b726e0eae052d0c1553afc67c7289a43851a4d04c2856cc46b4039380436465a3b19deb56e41b859aecaf22b90578a23288d5f7d9b0e" );
            iv_len = unhexify( iv_str, "9d154f3cc2c5b0bdd77e86e351220960" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "dbe575ea04b58429e68c733d99d7fb3a57e5604d6fc3baf17e0c6f981d78c070144702861316f892023515f20b697a8f3a40d821162dc9255d4775e7578285acf2cca67e902c060f80eaae29b9c011b6c110371409d914782e1e4115dc59439a2823507330852f10436b121538f22a3b619075610f1da87b6035138d78c75a79" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "8698763c121bf3c2262ba87a40" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240104_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "ce1407f666f2aa142ed4ef50eb2a4f64" );
            pt_len = unhexify( src_str, "585fc1e86809247826f87424741f6ce2ce7c7228fb960803be643acd28332b2036715e2b639fe3f8de7e43e88bd8e65a6e2259391360aaf534ae7566cbd2b3961c874d08636fca117d4123b3063931d7a161d00220014339ae9f447f31b8a2d7d5466fb1ff2508397b5fa71f9b4cd278c541442a052ae4367889deaed4095127" );
            iv_len = unhexify( iv_str, "1225a2662d6652e3d4e9c5556bc54af4" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "8bc13cc1cb52fbd15390cb5663ce3111c3fb943f8ed3c4f07b7aeb723649fccb90895999ec5dbdb69712d8e34ae3f325fefa49ecc7c074de8bb2ea01fa0554d7adbf49498f2f6e78aa0cd24620bab0f11bf9b2c73ad0eff780eb6c03ee9c4538952af754c566aba7c717d1ee6ac2f5ffe21dab9afd649cd65313ee686596fef0" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "9a1f1137f9ed217815551657bf" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024096_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "5ecea1da76d6df90fd0d4077ef631b17" );
            pt_len = unhexify( src_str, "d87e9a0c6a9796d60ed78924f7a8c408d5b9fab03fc76790e74029f13358fcae0035bd971a400845f508c2c2cdc3949be498193afcca6d75f8d21521ac673bd41a936a133fb5ed61098f3cb89df5234c5ca5ad3dbbe488243d282412844df0d816c430de3280ab0680a2a5629dce53f94e8eb60b790f438a70fafb8a3ed78a1b" );
            iv_len = unhexify( iv_str, "7d7ae2ed1cfc972f60122dec79ff06fc" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "1eb19da71857854420c0b171f1f0714972fe7090db125d509aff6d92e5192353187f0906e3e8187f73709d1a60e074af01e83d1306d582a82edbdbebc797a733d72e2d4208675ef98ea4eaaddae2292e336fcd3fa85cdc577f4b8d3f324f0c5cf3919701208d6978f83466a02ae6cc368f57e18b9ee16e04cf6024b0c7fbad33" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "f74b3635ec3d755dc6defbd2" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024096_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "6d6de51c30692d7863482cbbaa5ccbc3" );
            pt_len = unhexify( src_str, "9f242c230ae44ad91cb0f4fe259684883968f3ca4f57a3e0cc4b03ab063a4eacdf63f9e7900a98073e345d1b497b985887e1ffb5fe7d88cefa57dd41076f2da55ce7ab0899bdc5799b23773f8f7a4dfbf1861cf4de377281fae9763dd4ea8dc7c0d632b874c86ac8e4c90339ec3f14cc51bf9241660ab828605cc602984a0f10" );
            iv_len = unhexify( iv_str, "c6c0fa3da95255af5f15706274fa54ee" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "55e75daa3df3b13a33f784d5adacb2ff6861cacb297d5eaa61693985b6a0f82e9e0b3a28d10648191c6e62d6260d8a8bb471e6b37aca00dafdb2fb17454660f90c2849a9ad1733d7bc227d962b3cd86ab32d5b031eb2e717e4551cb23d448e06bac7b2a4cadb0886fde472d45de39eca2df474ba79eb58504318207325c81813" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "8eb9086a53c41c6a67bad490" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024096_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "76b7f2307e9cf9221c8f3ff7105327f9" );
            pt_len = unhexify( src_str, "bc076bfd1ff7a9fb043a371e5af7112bb0c9c442be44ca648567937bcc091c127f02ab70b81ce51b2f7a38954dca3d94b3716c6114f0ba349d6f87f5efd84506ed289dfe8a1277a5d1821c56f9f297cb647cdf36d308e6ad41c55d68a5baaa520d11d18f5ddea061c4b1b1ec162b2d5bcf7c7716235dd31eda3dc3094cb15b26" );
            iv_len = unhexify( iv_str, "3cdaf7932a953999a6ce5c3cbd0df7e8" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "88c70d3cf5817f9fa669aadf731c0eb03c3d8e552f2dc763001ac94837353ab75b0c6553bb8ba2f83ef0556f73dae78f76bc22de9a9167d7be8e31da6e68b0f0bdf5566059901726b6f2890ac8745ed14f8898a937e7d3e4454246185124f65cebd278f8c11fb0de22da7248f33ef6bb82cb1c08259970714de39ea4114f85af" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "6006fe48f74f30bc467c7c50" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024064_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "bac83044f9d8fefcd24766644317c533" );
            pt_len = unhexify( src_str, "a72daba9de96bc03b5cd7449c2e97c858385475127b9614e37c197225d5789535b69f9123993c89a4815c1b4393bfe23754ddc6c01fc44cd2009b5f886988dc70a8cebb12664fa4a692db89acb91de6a9eda48542b04459149f59537e703e3e89f6d683ebb797fce3874c819d08676d926bf2da2f83a22449b89e204b5ece58a" );
            iv_len = unhexify( iv_str, "1307cd0e6f9ba5570e9781fca9a4f577" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "479cdb5f65b9baff52a96c75790e3b7e239125f94525068cd1d73a1b8475080f33451ec83789d7189f5ad6a9130e7aa4df10d71ecabb5ccd980d84d0fbfb342506edcf7298ccb310c0e297dd443ded77cf1d96fc49055534439f1af583217a5de36e4df036a3b640d0212658399b629193080d38aff0d4e8aecd6c8d8f48b44f" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "ca192f8153aa5fb7" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024064_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "627776b20ce9bb070a88f1a13d484550" );
            pt_len = unhexify( src_str, "1da4a24fb12538a724f62b277410d50e918bd6224d4a61df6fb7734300643198debea71686e018bcd8455c2041265d11f7f5dcec08c31fc94784404423bcf1dc8e615227d2b0840be123a1efb8201aaa15254a14a2d76a6ddf536701cb3379d3c6b1b0d689e5896186c88d4a2c53a70bb422ecc8e0a5c3b9f3d89ce40676e4f9" );
            iv_len = unhexify( iv_str, "57f3f9388ea1e2c1c73f60b7d711f6ea" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "f8a06eea528dad12b11ead51763aa68ca062f9f6c1c1f740fb910974f7ad9d2ac87c16fb74d07c3bd3b45f2e26af417e00416bdfee7ed0b69274ead70a52201c1fc05937438855f5564ec3e824daa0c59da1aa6f6cb8a44ab5f73d661b219766b80656cd3ff1e2d6909c6ce91fb14931af8580e859e9d7642678c1c35d9435d4" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "05b432826dd9b044" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024064_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "8954e2c0a7ea80fe3c8e75246f75bdbd" );
            pt_len = unhexify( src_str, "d77e11a837eff95c77dd56e9cd97f0ffcee0adcca4a2203d23ce74c804a75cef1bdd69b16228472a2395118dfce636b8916372d6a24106f9a168055c6d4b44264674ce3905b3b30f5108ebf939f3fa8f55c12e001b457b73669acd23c1dcabea05aaba34e2d0f66a4d1c9162764228ebc4d3974fdb38b1a61a207788c5deb878" );
            iv_len = unhexify( iv_str, "2b5f9420b3c583403d92d76a2dd681c3" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "35b8a04d6557426def9915eb798312a7572e040a65990ce15a8a6e5acd6b419c3fa26828b6efd2f1f50f91f672fed0feaa09a6ca6b4844fac5d3db571db8bbce250086b8c89aa6fa07bdca8dd0e1fe76e0f5a821145bafa11f3a9b0b003ad09de73ad71849ac58f7fd50851aa0fbbed17d222a0a5607f9f75dd3b0d3fa45a135" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "96511adc097838e6" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024032_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "7d0f9109dd846c47527a429b98d53301" );
            pt_len = unhexify( src_str, "506efc29c0f02910cc9f5b2e677bb811e366b9e4910c00b36e48e5d5b42718f3b6d1a08a2de9c6d4ce44fce00fb7e10cf89396a88bdb38dcb0dba69449195e19b72ff989666b366f03166dd47cf4c7bf72dba3048fa34329ba86bbbf32934a0992d72c463fffee94653379d23b8bb4dff03fd86cfc971a2f7cdb90589bbbcb28" );
            iv_len = unhexify( iv_str, "f58a5bb77f4488ee60dd85ca66fad59a" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "2e2760c649f17c1b4ba92b1fc9b78d149a9fc831f0d0fe4125cbfc70d52047f32a7f25c716533d199af77ed05e259cc31d551187dbc2e7d9e853d5f65ab8a48840f22391072cbe29e8529cd11740f27d11513c68ad41f4acc6fb363428930fe3d7c0e698387594156e6cc789d432817c788480f3b31326fa5f034e51d2af8c44" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "6ced7aac" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024032_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "034c805b5e83b59ad9d6a65ade3940a9" );
            pt_len = unhexify( src_str, "efbec09f8189404f3dbe569d3bab9b8bfabde419fc80abb3b21a07a5fe42326d23d022406981abd558e94f4debf38f2c34c3c315cb1ae1d5f2d48eae1335b50af9dd05b60aee724edb7d4e12703d5ec8873c55e3a3d6d8d5e4daddd5240fa3ec2d1f32442ce32cde66dfac77ed213207dc4838ca9782beb9a98d6dc52838831b" );
            iv_len = unhexify( iv_str, "b0c19448b9f2a818fd21ba6489c34fb0" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "a45ba5836011fc65882ba8b1d6bf7b08b17f26b9cd971eece86fbb6aac5cdfd42790a7c7390099b10dee98cb8e4bd8b3ccb3ca5d0b9d02f759431de640ad7f5dffb919a8aaa74695f94df8eff4c7cb242d643c55d6f9c8323006f3be595aa8cdbfb0d9260ad2473b244ca65a5df53d2edd69f47df608e22a68b05623150b5665" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "43e20e94" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024032_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "f3bad89e79691ae72f53964b928a09f3" );
            pt_len = unhexify( src_str, "01913e4ef10226d80c5026ba9243fa41edaf5f5c232d17c034db4c0c8369f48d89a1d58b3b2dda496506c30457365bdd76710173a97022d647276a4a8ac73f0e9e211cfd7d64849409ef61cce618675eaffe88b3f14496e5eb013c0f8a122dbf16f2c675edf7f813abe9c56101e570e208e651fd956e710dc09f13ebd22b81ab" );
            iv_len = unhexify( iv_str, "aabf77116a75046e7ecc51a468aa21fe" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "f7453670604ff6287ebdaa35705cf7553410452fdb1129a7fcae92565a4217b0d2927da21f3d1b2bd5ae9b7d4dcc1698fb97fc8b6622ddc04299fdebaba7f7090917776b86b2af4031fe04fa1b62987fa9ec78fbbc2badc3a31449be3a858ac7f277d331b77c0e9b12240bd98488a131dbd275b6a0ce9830ff7301d51921ba85" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "15852690" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024128_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "839664bb6c352e64714254e4d590fb28" );
            pt_len = unhexify( src_str, "752c7e877663d10f90e5c96cce2686f4aa846a12272a0aba399e860f2838827c7c718365e704084fbe1e68adb27ad18e993c800da2e05bcaf44b651944bde766e7b3ac22f068b525dd0b80b490b3498d7b7199f60faf69fee338087f7a752fb52147034de8922a3ed73b512d9c741f7bac1206e9b0871a970271f50688038ab7" );
            iv_len = unhexify( iv_str, "5482db71d85039076a541aaba287e7f7" );
            add_len = unhexify( add_str, "4d75a10ff29414c74d945da046ed45dc02783da28c1ee58b59cbc6f953dd09788b6d513f7366be523e6c2d877c36795942690ce9543050f7ab6f6f647d262360994f7f892e9f59941a8d440619fda8aa20350be14c13d7924c0451c1489da9a0cafd759c3798776245170ad88dbceb3cacde6ba122b656601ccb726e99d54115" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "c7ee1c32f8bc0181b53ce57f116e863481db6f21666ba3fa19bd99ce83eee2d573388a0459dfede92e701982a9cc93d697f313062dbea9866526f1d720a128ab97452a35f458637116f7d9294ffc76079539061dfeff9642a049db53d89f2480a6d74a05ff25d46d7048cc16d43f7888b5aff9957b5dc828973afccff63bd42a" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "63c8aa731a60076725cd5f9973eeadb5" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024128_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "5f2af1b14ca9598c341785189ac6e085" );
            pt_len = unhexify( src_str, "790bc975865f44e3a1534e978e90b064530321a2280a9172dc7f3451773b01d4a56c1857ad0474350b945e4f34cd677c22ca89445a564b47a8526d31d18160c35d2be1e89428c3593b53877cea0d88d85b2a7ed0552e39a0e96e35ae0384a5d7868243045dcbfc245a3eb3ff99f4dd86c0a314f68d1971e773caf9c168b0aa0b" );
            iv_len = unhexify( iv_str, "bbf23307ad2718398b2791c16f69cc45" );
            add_len = unhexify( add_str, "26b160695de2ba40afca6bd93f1c2895f92ca9108847a8ab71ad35cac9f9c9f537ef196c5d41b10e3777c9a02ad3c73cd299a85f60e5d02794c3be2643c3e63f105b94d32cb4e3eb131d3f487fa5d1de1a4ad80cad742704ed5c19a7cf4e55531fa0f4e40a4e3808fb4875b4b5feaf576c46a03013625f04331806149e0f6057" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "52c373a15e1bf86edfb4242049f186029b458e156da500ce7a8fc7a5fd8a526191ac33e6b4b79b36fda160570e2b67d0402a09b03f46c9b17317a04a4b9fbe2ddcfc128bd0e01b0be3fe23e51b69c28bcf8725b8e4208aefb1cf34fe91a2bb6d5bef7b936bec624a8f38c9cd4ac51a0187635138d55da1fb1791adfbf8459d3f" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "db3bbdf556c9c1be9b750a208fe55c37" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024128_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "02980dff205bfa5b18037486618e1fbd" );
            pt_len = unhexify( src_str, "f037ae281e45c50c9fa875f0ec9eb43251d3ae1b6acde27cb5edda7a4e384f50301a68bb6f4caf426adb31457c5eeaa789edc84fd902cb82e00dccbebe272d90cf690ca82ee748885f02daf377970e985d55994fa668fc5e3e06763e6829059fe0c3eb67033b3f5223cd4bb654484c57370d2b856d7117e32ead3d179064315b" );
            iv_len = unhexify( iv_str, "27354e68a004b255a380d8480dc9b19e" );
            add_len = unhexify( add_str, "37eed8620136842938ee3c3c08311d1298d3fd3f0456c056e0851a75d844fe6c61aeb2191c024ffce38686c09ab456f0ec26bd76f935d747002af9b47648502713301d5632c2e0d599b95d5543ac1206170ee6c7b365729c4d04ea042f04363857f9b8ea34e54df89e98fef0df3e67eaf241ed7ebbc7d02931934c14bb7a71ad" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "f8090d0a96fc99acb8f82bbbe58343fe227d3f43fceece5492036b51ac2fa6db4bf8c98bf28b40132b1ab46517d488b147e12ceb5e6b269bb476a648d8a1133d5e97d4f4fbdfa3866a04948851cfb664f3432de223f3333248a1affa671096708ce6e2c9b4f8e79d44c504ff3cd74e8dffd4ddff490bcba3abffbade0a4e209d" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "b5762b41241cbee4557f4be6d14d55d4" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024120_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "1fc9bcc5aee350f1ef160346b642cc20" );
            pt_len = unhexify( src_str, "e0fb08cf7dc901bf698385a38e1a81acd4118f083e52aa52e1ded16ab1e840cc49fa1ead3292ce21096cc75c89dc3701102b0982fd3a6bfa55a7799e579aa7336edf365574a904bad924ec080b093a604994db4dcd8323d7d39c3c35750b0741b170481539d22551871d6a0e2ea17e4bebe8ce19ec3bc3bf4f6edae9cd7ab123" );
            iv_len = unhexify( iv_str, "910a81a5211ce0f542f1183c08ba96a7" );
            add_len = unhexify( add_str, "2dcf7492c4539d6abc3d259ba5970033ebc2e7ddfa1af8be11f81b459d7477f310be2171290bec2f2ae2cc51266f46e98c878dd2444afefdbdb73a417518f5fd4c116547bf442fa9a8cb2300c5ff563117b2641dcd65018081e62a7ce5c4d822563824e5eafea90cbceee788ed44e6c4f23fe8926603a15adfdb556f11a0be9a" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "514d27f8413d7ed59d96c14e7e74b9f3d4518486876c469b369f8c5734145f4aa52506c8f832d4811e5f981caadedcf09875033c5b28a00f35605d773c7f9e1af7f0c795e3df1fa9b5a524f1f753836c1e2dc9edf1602d37ac120f3d8a5c093a5285dbe93957643a65f22995a2782bb455d23318f01bd18ae0d0813b01d233e5" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "feb7a25a68b5f68000cf6245056a1f" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024120_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "9cf329dc10bcebb484424c77eb785aa2" );
            pt_len = unhexify( src_str, "92728a696b07704fb1deb648c5036a1c8602b4006fb2fd2d401c4b6692e252c7f66918078542cc0b1a97486964276d6e6c77bbb88a9fff0285aef70783d9f2be3b7b22f8a8c02771492150122fe022722bf64263f5d2406884108d8d608273bc02a9127fe4dbcb321ac44a7d2090cff7017d59d73ecf927b8b05968675a63ca0" );
            iv_len = unhexify( iv_str, "a430b979168f5df5ba21962d1bd6dd15" );
            add_len = unhexify( add_str, "4d94b7650297c66b43210c84e6e7b09385117ed8fb91adf643b2339f39a5d8dd0b0d75a793e2a669e42c5ddb0873714e01cb65da9eb73fd976a49ae9a4762bcbc06be5052f750d110a407764280b510da5fd0fdce969f86ea6bf52ad4fd9e2d81ec5cb84af0a1d406504a34c51c751daebb4421fe1994bf6db642e64bd471d9a" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "c13dbfc60b34d75f8a84db1f6aa946dbfc19479d63900450389756cd1ada8f6d2d0776607f7053db6bfa6752c4b8456f0ace314ff3fd4890d6093a4a5d47dd8fbf902e3e3000f5e02ba93a00985f29ad651cb697cc061d8f3cc74e6d8d0743a1988947c9dc2305e2b7c5a78b29400d736acc238131700af38e72d8c98ba007eb" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "82f1dd58425eb9821fcf67a6b35206" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024120_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "cf43ff6a1ef35c37862ae3b87171a173" );
            pt_len = unhexify( src_str, "a1e670b3fd62039cf29edb61b26555bcd0f9184be4593bf6b20ceab263bdc76cdef34992fe0ce4d43bd93bd979b78bb252c120fbaafe4947fc0ec05cce4358a5089a841c7476b0ebfca6476e690cb9ee0b73c6700aa82aa8f4050f2c98500052a2d3274b30b0be67549d756efd163c4369b6df0236d608bfbecd784467db2488" );
            iv_len = unhexify( iv_str, "6c56540b3a9595f3c43f5595ace926bc" );
            add_len = unhexify( add_str, "5c0bc6e44362299642f3756acf09878bb05549eb6cd6c4942d39fe586ceac228d2aa9c92f8393e5017e73ee41002e60aa8b993c48a7638ce2ae0ae0eaa536bd749b07a8672fc620a5110af61232b6a3d527b36c86637cc1fa92c84008465fd861920884d8a784e194ec52fcbb767a68ca6fabb64ab0a0d680963140d5cfd9421" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "8ad36522e4ad47d4a54c5eae0a8b9ff4911aa5b9b13b88b00488a7b678f63cf85945b8d4998d1007e27529b56f50b9e3b373bb6fd861a990514743b9707d535b40d1bdbc3f58a63b8ca30dd7934ee98ec3325d80afaa37e38b4e82d8851166589027d91347727b314e02ed08a7846e29fcd0c764834d12429d9f568b312081f3" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "f5bf21d5eadeebdef3104d39362b85" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024112_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "a0ec7b0052541d9e9c091fb7fc481409" );
            pt_len = unhexify( src_str, "5431d93278c35cfcd7ffa9ce2de5c6b922edffd5055a9eaa5b54cae088db007cf2d28efaf9edd1569341889073e87c0a88462d77016744be62132fd14a243ed6e30e12cd2f7d08a8daeec161691f3b27d4996df8745d74402ee208e4055615a8cb069d495cf5146226490ac615d7b17ab39fb4fdd098e4e7ee294d34c1312826" );
            iv_len = unhexify( iv_str, "00e440846db73a490573deaf3728c94f" );
            add_len = unhexify( add_str, "a3cfcb832e935eb5bc3812583b3a1b2e82920c07fda3668a35d939d8f11379bb606d39e6416b2ef336fffb15aec3f47a71e191f4ff6c56ff15913562619765b26ae094713d60bab6ab82bfc36edaaf8c7ce2cf5906554dcc5933acdb9cb42c1d24718efdc4a09256020b024b224cfe602772bd688c6c8f1041a46f7ec7d51208" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "3b6de52f6e582d317f904ee768895bd4d0790912efcf27b58651d0eb7eb0b2f07222c6ffe9f7e127d98ccb132025b098a67dc0ec0083235e9f83af1ae1297df4319547cbcb745cebed36abc1f32a059a05ede6c00e0da097521ead901ad6a73be20018bda4c323faa135169e21581e5106ac20853642e9d6b17f1dd925c87281" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "4365847fe0b7b7fbed325953df34" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024112_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "f9ba053776afb01d15915e7f82a04f21" );
            pt_len = unhexify( src_str, "fb59858421ffbf43d09415a77320cc9250df861e4414817e7b78cab918fa890ea0400d4237f7ebf522d97318ea79f9979a73970296827a1a9690a039e6c605a0a3efc0077156e1b15f14d88685833e09f6cd6f783d0f50579de7a30907b9d8efc4c650ec57dbf7b425ffaf9a900ec91087d470409da4d67cae7328c15a5db1fb" );
            iv_len = unhexify( iv_str, "df26b109244f5a808f3ea7137f2f49fa" );
            add_len = unhexify( add_str, "b21c8101ac96c41bad2925b9b6c863f54888f36e4995820ebd51f53e323e46f528d91f4318183be0282312ccde8da075fc2e82041cb41a79e9933012a4cb6e9f89717444bc734da3b7e40e903e58dd0f38bcb115684227ec533c09a93c89c2c2584bbac83a4648f82b4c9207f43b61e5ec470602076ed4731756c87d4e0e24af" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "2c306fc60bff58308f2b9f08d52369e87119d7f6de2279fcdea0c46c901c8dc5b4f83578b17a00786014a17d3e380e1af4b9f32fa58b9ac763bdf86ff0c6084afe413a5dcb7617f94d76e59e370eae4829e69bcb70f10545b04ed5fd137e1159f3961b2c01089ebbe2f16a91c782d4f383fbd4d61b66138319b63d79ce9fdec3" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "d6db5aa539a6e2e70885508d637d" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024112_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "fbbc406a669b94374c7970f2ac10c91c" );
            pt_len = unhexify( src_str, "a9f334d1ae7d2960f39da4f1df85830d27c0f13fa0bd23d607ace4cf58b359584120e7c90d3062b1b23b1a9e85a740c9063ff80423b5846257e4426c174e8cd77a3dbcfe12970ebddaaa00a8ffb554b2a80decc81f9917f5a1369e8bf7288ed868457993f480d8aff0b92b3db2fda233e32fabec1a4514715364d4f70f98d62c" );
            iv_len = unhexify( iv_str, "46152f5a68c03dbe2f28e69f5b52e2fc" );
            add_len = unhexify( add_str, "1052f8b2d3e11da53ba9efe02ce985098d171dff9b98cbc2f6755fd88214ddb8660225a63a1c8bcaf43ff3930e239824ae8e122068b89d7fe73c658ce030cb51dae9836aafb68fad77b1cb5bff8d7d9c920ec449181e10ea643cc73abb9620dbdfa32e06c29cfbd8c7cb8b1103763616ae6f9b19c4a6e1eed88c3971c4778c2b" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "7b16424c508da3fed14bb53462d1805f0f9d09f803d4e166fdadbac76f9fc566665554317431642f6e527123ea6c1c0ddcf45005213b0f2747321fa112d7b893cdcf4c1a59e8bd1c48b7d77881c6d79de3d850bce449969305797196d187196d0d81dc3423295f552d3c27d6d70e42c9a1a744a039181e733450c9985c94ae94" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "b51dca8e00988af0987860a663ad" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024104_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "fe96eab10ff48c7942025422583d0377" );
            pt_len = unhexify( src_str, "194c8bbbfae4a671386b8cd38f390f46f9df6b8661b470c310921a1c858a938045834bb10380037fbf5f5e00688554537be0fcafe8270b9b59068fa056ab1268fc166c2d729243a06650a171c929c7845c85330c04568d62977eedf3b1ba9dca13bdb8f9522817c8cb99e635e37465ec1c9f6f148d51437aa9f994a62e1bd013" );
            iv_len = unhexify( iv_str, "97ce3f848276783599c6875de324361e" );
            add_len = unhexify( add_str, "127628b6dcbce6fc8a8ef60798eb67b2088415635119697d20bb878c24d9c6f9c29e148521cb5e0feff892c7855d4f1c0bfb32ad33420976714dce87a0bbc18e4378bd1ef35197d0ca73051148f1199010f63caf122df5f71ad8d9c71df3eb2fbe3b2529d0ba657570358d3776f687bdb9c96d5e0e9e00c4b42d5d7a268d6a08" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "12495120056ca3cac70d583603a476821bac6c57c9733b81cfb83538dc9e850f8bdf46065069591c23ebcbc6d1e2523375fb7efc80c09507fa25477ed07cee54fc4eb90168b3ef988f651fc40652474a644b1b311decf899660aef2347bb081af48950f06ebf799911e37120de94c55c20e5f0a77119be06e2b6e557f872fa0f" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "6bac793bdc2190a195122c9854" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024104_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "f2956384a65f9627dccf5126141c7bca" );
            pt_len = unhexify( src_str, "89dfd185bc33adbea0c69b55d37087de3fa7fd69a9fa76aa1568ac363c5f212ae92d202b9338ef397266dd8bd1ef36cab6d1368feafec69a4e3e11e1bf1beba35d96e040d91e9d3a838966bae62a15b18d621f33efd9ec511de4bd287c722cd39b4ba43e7a6f8c8ab672d69eac6b21a8d3544ab1d64f9de31956b93b1104431e" );
            iv_len = unhexify( iv_str, "2f61f76bcf074a3d02f51816c0411052" );
            add_len = unhexify( add_str, "bde1508823be7984d5921db4cab1ed3017c0d73cb9bff9874f39a6f5bc449719c1c43d8fb4e76f6813b0985d4b124517f9e4e2d3c552b2f75876563c93a44c18fb6523ee732ea5b6d13417db45120653df3820a32ebdb42d544768461b1d0b55b46b09f688e47240880930fca7097ddfae35f854891e21891dbad13f661a2534" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "023a9c3ab3ed0181ec8926e4bfbc0fa63e38ec8980eabd2ed75e29b681b3ec04cc8b27fad3a7ce6dc1efd680479a78f02de7ba92f45dc03de02852a2e67b35bb1dd154568df7acf59081dfc05aca02c0aa9f3f7b4fd4dbdb671b1b973a48af0c325a23467ba5cb59183540f6edf4c00376be39a3a672feb9e795d1bda96f0017" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "613eeca3decbe09e977e0beeda" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024104_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "2e9bb30ea25f50b3e7711fac05f9d44a" );
            pt_len = unhexify( src_str, "17a52f4faa608dc9853d4511feb3dd9d2fb92d7a3deb3f8a7a6df3fa2a909b7db30babef12d9da71aadfad16bfd2bcb5706ef2addc58eeb8d8d13f31326f7ab1d0aabfe5525014f05cd8fb80e1ecb0654e62078440157df66f618f078cdf2b322b0f8878bcd924609c33e42059aa69fe0ddca659aea42ab907b483aa55aacc63" );
            iv_len = unhexify( iv_str, "9668e8b1ce9623ad52468431dfbed632" );
            add_len = unhexify( add_str, "f776c6e892e373ec86ccf706704d47cd89fa45c2abdeb0f9f6f32cde88c22f001150cc66f0fd83e9b75b97bceb98913cf143cd8a68bf06e1125031e3e7f09dfefbcaef4f04d7bf28aca1992a7e4228fd4017a5b32fc48101c8f5a609eaee9489d02200e8a13efeda60b57df53ccf2fe26309a1c1e1d40db6eb8431dbfe8d43ea" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "407171db1dfb7ff20d5c97407375574220534ef75ba18dc616400e5e967e72db23783a6eb9506b611d0c67a83f5c423380ceae66d5dcdffc31e31239357b91794018e9c4c36c286f7b17ee911136d9cacf564baf5f9b9831779375e63aaade8734a91bd4000e53e5e412b3f92f8b68e0b7ad3bf6f274744e2c5a635894bf918e" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "2741ebc33a4d4c156c21385a23" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102496_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "aa705ee70297e9212f70585d92f42aa4" );
            pt_len = unhexify( src_str, "5e4b47d986d55f49708cb3e4d27072a7e850936b27b24723856acec7b2e03caccd98c2a002a2dd1d3f4dad8827a5910b42986cb00be7bff47eb401be5f324cd2cd3ea2fa41f4ef61f9771a4c0184d85d6023f37f3f54bb9d7cd621fe36ce11a82678a0754a33049106be597c53f287692ac5a42e59f09a2a117fad6c034a91b9" );
            iv_len = unhexify( iv_str, "89822c9db69229d1e4880afd19965908" );
            add_len = unhexify( add_str, "fdd655584a92e29a14a368f28a73f9dc608e5c2ffd308d4aeff7326bbef5ea58f84620c9ad43c0b598c271527ae60dae6db4ffd3f590e503ae7057d8c48e9b1bd8f8a8832629bbfc1391b954a4fcee77d40096eb5dcec5e0439375ed455378d716ee8f8b04ccde3291e580068dd7dbef4ba3685b51940471f24859f8e93b659b" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "0f34bb4e2a4016ba41eb23e7688edd455f2d46a5097236d9a124ae0bd47349876319976aa4c3aa41680a63cea85f433e3a1b4376f79d004710d486a3fb5afbb7db2c41aca400e04f75ba91660bb68354029defeaae1853447f8fa0d470b25371da73c9e8ee841ba95fc273f88c2e4604ff29a131a7d73e60a00340e886df5359" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "a247e88acbd4e354d7c8a80d" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102496_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "ddeec78a0c23e8c5c32d3d4f9830f927" );
            pt_len = unhexify( src_str, "134fd6be1a934053a539398aeaf5d3aceda3ef722a6b3568af6958a4b1207f7e9b9e835cfd46a7f3d4faed829ad23554fc7c0d1a9b32bad9477d9dd397a259cfb0bea30268aba7b8cf4a35dbf99a6b2ca968649847f717749bc5f41374e1574ad6c357f7b60b0cffcb822bd3924208d0472a973ae97550b921338792ca88fde6" );
            iv_len = unhexify( iv_str, "ae428ebb974ccfbbdbcf6203105724f1" );
            add_len = unhexify( add_str, "e3d5ce768c688e881e72f036341b2d91947e02b7327eb53240c85b0b93a40eb0f3346817e2c9e126209b31b57633c4384f7af46846d9bbe6fd0d6babc57b84d0f5be2a8a7b146b38914a4cea70273d5461126cfd7527ab397510176e790300a06066655907d499bded79f5bb39f6fdb03f85a415c2cc2ad1f25078f0da7df215" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "865d6148c9820b67c08c17c9214de612ada6e24ed67933d13c3b3ec43637fa305673d8d52d15a195b27a6b2563682a9f98912908668e3335192b1daabf26e1e73d7d34764af006b0c14a0ffad3b6a0def59964b11eb52e829ad790069997931d09be88b8d60aef90e39dfcb0df4fd54b71597b8ac64670e703e7cb83efa3f2cb" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "64b2458a6eaa6f12937a8643" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102496_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "829008339e983918b8d142091f84ee28" );
            pt_len = unhexify( src_str, "6f30604d8c2fae216b1ed3d67485631eaada68fe89a7020d6e29f42b937e7640fc1f23c00ba48bf239740f6468289ed211ba81e809cda55fe067bdfa198bf0461daf86d4a7969de9a629513809b358630ce7eb50a783b8c98ec1bd5e56cb47032ee8fc64a939dfc4a870ea9419b16178109f1966ab964da34debcf00cc49f57e" );
            iv_len = unhexify( iv_str, "dc62cf12b6d0439578b457e516d8205e" );
            add_len = unhexify( add_str, "e700cd917923b16c968712b2fdbf08be1b5c3b5d9e42cc45465549898daa07c44b4cd321ba16a38aeb6720e217a58428e3a4cc125920cb3fc92f039b66716543bab71b64ebedbb1e5e3e8fbbecff3385ab0ab16b7f6554b7fbb3b4c92307c654361f984d5a6cb69b8708684d90bb1fdfabc0cb59f42c2b3707b3755a8c7abf34" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "adf60c4affb2ac76cce20cf9f302b909bfda1bedc60be21b53f65d0b81bff08f7e90ecaaf12ee1f9d921926b75e244b7e8357c1cfc26013a6d1c874ed2e5cd0cce012bbfff0dff85b372d92c18dce887c1651b6467f173a67ac8cea194a6c41e77842675f60cacfbc9c81597a08959d19af632d3c191bf69505620e4290bb040" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "6209c09dd1b7ea85d02eb9fb" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102464_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "4aec55c7e4bb36c32cb543b57cfba3fc" );
            pt_len = unhexify( src_str, "4cf1443a5448fd09e09e91b7cc5f8e00f53f0b75a6b17db5ab9a721167de5f7bc5de1fb711accdafb7f3f1bf6b98393e5f09e9091e26d1340122edc91f7e60f62caa218f1927c8f0032be0752520aa650f6f1ddf40412c96d49dcc2287ee17834504f1dda3f4a723e2fce064f0b8dae0789ec455922a14488623e3ac10b6e312" );
            iv_len = unhexify( iv_str, "6669c3022e0820634a95efa2b5578e93" );
            add_len = unhexify( add_str, "f6ae9b1aaba18acb741c9fc64cfba3841f5127b1cda5cbcd48af5987428daa5782d2676bc3e2ef23936ec29a80d6b5310282b39b77181dc680799ac9c8125fc48afd185cba2ca8900bd9a0039787b4f3a6846f3edf5f7b921dec2608fd3df67600ae0aba9378da0015bd57d66d2999bf751806d1b89214332bac50f721ca9474" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "720c32b0d454f086af36a32cc7274e2f2fe08db9cf1cefecc14b42b3e5c573aefa7e9e1ee0042eee21104dc3e4d19b012099280c5a53e40a0bf662d8295dde743143a28be7305729767a37cbdf08fb3c87667939a8ffe44c96ad272e30b75aafada2963bb9636f189c37d976ed1c458295fe85ed19662c463d7c8155e9f04115" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "4b3343b627095f60" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102464_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "8629e8064b3ba2b95bc20dd075f8e931" );
            pt_len = unhexify( src_str, "85896de4b6454acf8568ccf95ab68a632330ce71ca8b4e7bfe26ad8d7e2e6b63f2032e2cd365999ffd24ece0df16904d749d06e829a291f3d07fccee27d9c6f3ff3a139d9e33f0660803de8fe79dc6ad291fad47c93543522a1c38e40697426a9855255e3e0abcb84d474ead15341c6b235ccd755e58fe6e87898d216d65abac" );
            iv_len = unhexify( iv_str, "dc4bcefe284cfc606f39b057b7df411b" );
            add_len = unhexify( add_str, "abfd0cb6fee8588aa68606b7e487bb9c0d2bd11205611a6f30a78d9ccf28e827cef4e966fa245e4b7b39533a4bd00176ce3c97858b0c8abdff4c548c835bf1962a6115c4ce7c05b1ce5aa29b412e816abc925b8cb998eb4b69c43a7dda1b3cf0d728072d42cb5a489db521698c5daffc3013537bbf622ef76a2e96089b7d4b96" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "b295ca0d7707892fb08537f42d28a844f5877177f136b4620f69b05c83f43bf2e61323e80076c88660f5385060228bdb91d866686e691cc7e96fdaff41f2ca5f5b5d93ecec7bba82515a6e0bd604c99ef93d3ea013d899464558bc822bd765eb1ca2b8b8a7d961a6a316bf135c22d2ee552e62d8bbc5b60ca31bb53cde82fb5f" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "d26cba11f68a5e1a" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102464_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "4d901e59a491c86bf538f7b38247bb21" );
            pt_len = unhexify( src_str, "4c370a9f316d25702195409d8e73bbfa40aa15c2b0ea55db9257a9ae4e8dccad14589718741a78e5a74c26a801857e388c9f141ef7df08bc01384b2b2338c38abce51d547056f4bbaf7484f9edc96df122e71f132b7bcb6484228c3ae2f741a2c8b9b208b6f49b07081334b93c501938808cdbd2e40cf95ae4f27a29e1121480" );
            iv_len = unhexify( iv_str, "39e2788c9697e82cae0e222a9e413d8f" );
            add_len = unhexify( add_str, "48d7d20e424df3c3efced29e860771647ae01312a96e68d33f982c540e74160a7fbdb623d4b19abb1871d74c6dadc56038954b154389b752bebc40cf4ee1505ec8d844e1a04dcae430befdb081cc84252e0840f5f5146ffe5b9594f856afc2edb33b3c6f9041c9631c5e3d812959c5504938635f72c6fe29a25bbf66a4ecd211" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "262718671dd0e2c9a40b9d7297c7f6a26cd5fe4f301999a32059812719896d3a2f5350f6ec20d999fc80b8d7af5a421545b325de9180f14505f0c72250658a5014768fed63ab553de0fb01ab1368356043f6d1a6c9950c80e3d9d4637bbeea44c9d58a4148bb10974d507c62b67cc4e37eaebd7eb8e67077856cc5d1702f8e2d" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "bd814b4584941681" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102432_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "2f54229167862034ef6c5ff4a1246697" );
            pt_len = unhexify( src_str, "af2c89d3600329779abfbcf5be8bb83c357d4d2435fc8f4c413b956b898d22a8a889db9e2ff5e7229d7495576989695a0b52d796f9a23e9570b7caec6b46059749c29a293d31a6224baaf73711bc0e4a587abe9d0379adec6de04ce444676dfd8672e6660cfc79d7ee2e7625ce57dd4681bad66aa29bea2baf936122c3db17e7" );
            iv_len = unhexify( iv_str, "8168ef8ef278c832fc0ec846bc9f62e9" );
            add_len = unhexify( add_str, "abb9ed24137915265bddbd4b63f1d02efa2a99c8c373f19077c7e1c389feae36a7af42c661b0adc5dc8e4b5520d334e8e0e112d42c2977fa23485c0a85aef83f1e52d6749bd29cbebe14aea6ee1c1098aa96c6360b0192894bb2001c7c0fed7f00bb84953c23bfdda00818d1568fb94c1bd971982d6c01c12a35ef7af34f947f" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "cd6dede25433fd3da6137001219b57aa54bdf6039a5a8d66138171b006194fe3e13d484e5cf57a1acdaa8e76f001df7bf41cbed2c5561a37a32113fa116d0918167c29dd9e7d46f7c18d9db33d7f1bc33ac21d159ddec57a2e158f0c0993c16dbf50582371100a8d7c55cd47c03473c5770ad562240f754c99d95ec593dca284" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "4ab63349" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102432_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "b7b52fe74c5c3266edf731578d28a72e" );
            pt_len = unhexify( src_str, "01a4b7da57c0f7d9aea51283004b23f899669dccd6dbaec9cd6e747c7adb52432c7c29d1411ec1df4e5e33311ad84218075dabe17f73c95511ce7950f08b618feff56bd452b33455a1a03caa8371dc7fb9aebedb3cb652d94e06bd00a98bb06d30b506d41cb516c759f6d7f793472e6d6dc9ae50cf3dc8b1ad3d0517c4f555a3" );
            iv_len = unhexify( iv_str, "a005750e9f8c68ae238668f0a8f015ba" );
            add_len = unhexify( add_str, "805cf3635f9d84c7608c242ee23a4837dd3f260de9afd6166b08164a0256200be9b52e5259a4a54186ec067ddfad90f5c4f92afd1c7e4f2d8443312ba3c4818b664439a02644e55467045071aa2cc7939a940e89cc52c8a53623bc6473bf843a4e0f00149b2ce1543a6540aa0d9c2c5b68ba2bd5791078deed1de3b5f48257c5" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "d6124da0896d99fc7f2c3688fbca164f8fecd75b6260162c4dc2d2773ce75cf41a8c7a57998e0a7e49cc71e5ad6a04c7415f8d4fd11f1035d3a02ed744345d74ebc9c4f202f65bfa88d55c747fe777225e218f2149da22b53e6584823dbda42cc2dda56fc72b753f3923c443eb5c656515dd824d8c08cc78152226ed8c1808db" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "60d86287" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102432_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "7a3501d9fbb86ab80f5faeaf8876b7c1" );
            pt_len = unhexify( src_str, "4f0dfbd2aeab70c80814a1f261a1fe442eacff5d267fd0c0f93757919810f6610113f1b442270afcc47f2fa01ab01797683ec9267691a0dec45033c57f5cbdfcafdf154fc99e6140176eea92503b3f6fee5dfa5aad05f802e08a08f10e49a8b32a50c028f2bc7aa451be3747d10b96b3a1105c67c5167eccdc18b4a9b0612d03" );
            iv_len = unhexify( iv_str, "6d59be1833e75ce7f54ddc91ad6f5187" );
            add_len = unhexify( add_str, "3e556b1b33c42f1ad6cca67dabc6ff79d6cb667527335858e26cb4f6a3d8503ec415968ba97d2d79a3f80c1a10d75174eb5294cce8b89224eba7dfb258fb17cb5c5db7a914ace06e94cd2f2cafe3febc8adc4c2264afa2db2c6356e4c3e8667393a77a0afc36be678d5c0a4b63ae82d9922bbbc60559f331ece9947b67469469" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "615ea4535f1e579d7aa45c011018f272c2e234c3ea9e2d102cfaa4a437c41e64bdef7a211ea4d858bdb656215e600911435ef9c8da68e8239e4782ced7e7add063f33f5bc62b85d9ae44ed1b139580118c5fc054ead08257b0a97632e8c503c6219294af423f0deb36758e05857ebb05c6835972488306ebfedd2ca4ce3b2c48" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "74c6bf0e" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800128_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "195ddad2b0da195ea54a9dad0f86c161" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "265ab1995fac4fca7c2b26c84e4a2dbc" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "930f719034b76c232619ef2792fe6e65" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800128_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "12be48e90c849063637b1c2ab0f2b467" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "0020c3dff2f6f3acaaae982ce38f63c3" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "c8891f32b8015024ca42536d633b1863" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800128_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "8e792fc91675d5efd4d80d5a06378d24" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "15ad63b969f8e313eac3c717ff9a994d" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "de9a04b030954b0141dd78ffc67323d6" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800120_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "a668cfd45b6ef8b766a4bb187d0824d1" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "a111e94a6426ad9b4362132052eadf4a" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "3a3331e6a41cada2cca8e856135549" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800120_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "f36e07f2689832b914e0b817010c528c" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "654104f9d16348231e6ba6fd30c1f02c" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "be897583bae073f42138d64e622c35" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800120_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "25d839a709d98ef9c0c9e78ece961eba" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "b64537609040790ff648d51406710b9a" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "4d5854c69cc973be8de41d5584407c" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800112_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "957dd619f9f19445c374ceda9e9ac082" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "34887be03b4d4ca8ea2261b600ab0b0e" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "60e2d50adff707d8b279bdedb277" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800112_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "a5c9a2dcaf576e67828e806082d8e780" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "f93732aac9448c4a427e634089d7edcc" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "f67ed1c98bd2c5f3a738e75f15ac" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800112_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "0a30a816e8d4d85d40c8e4d7c93b777e" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "bf1f332aa19682d05cf95f2b03d26af9" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "acfb2f7884bc496f3089e50dbf42" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800104_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "b45a16bba5fba362704149dc56ba8a13" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "64cca850412091bf4e120ccd612df353" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "7b1adc23af9be185e5ae0b0f0e" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800104_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "0cbcbc1c72aa90e3ea7e2fe328d79723" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "2fc5fd964b45082546636ae1e208a937" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "fe091a768c731e54e2237bfdc4" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800104_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "94297a1ad3f0c333cd9b087b1efd43c0" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "52ec9dc82131d7b1c69c01fed6aada10" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "5c927dda855b76ab8fc077203b" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280096_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "1e8cf32008bdf867f0ff76e7d7ec21bd" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "3854b7412de72fefcc4b0c2155f6910e" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "cc8e7eccc056b06cffc307e0" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280096_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "2ce1a9bd93fdde2adfd8c2c16a395b95" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "64072313ed36eef8209f079fa622d7f0" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "cd9e8ffc1423270015bf8e8b" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280096_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "b15354ad3d874fe472719ebccd45f123" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "1b2013153290edef60a6a438bd7517de" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "f65a841ed510becf52b1eae7" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280064_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "14ef129784776647eb3fb8897915ab9e" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "f7bbe9f699156549935f2b92c1dda163" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "dd10fa64fd51231d" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280064_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "5d4470053c46a577bba7000075e9bf2c" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "854b768fdd7492c21618ca716bc8790d" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "1f3c73722006023a" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280064_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "ea87d675a0d406c57f78a2531bfc0c9a" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "0907503fcb06ee384526f7206180a080" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "65d5466392b63bf6" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280032_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "d3e8e27568e6e17ff807cc207e5d4eea" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "18e51cdfb4a3a5ebc7b0d7b17727aa95" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "a7e3f637" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280032_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "596a602164b1a0bb50ef91bce3a98796" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "2025e72bd6a511980a8ddce34565d16a" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "f84f92de" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280032_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "d0194b6ee68f0ed8adc4b22ed15dbf14" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "32ea8970a8cb70d6ffb3972a146c6984" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "eef4b97a" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024128_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "869ce65e5e5e12c620076365f149784f" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "317bf07e83c2e9717880b7d080957fe1" );
            add_len = unhexify( add_str, "ee185d738260de67f1792a7d548ea73267fbbb6543bc081fac43e00e6cca92d7d646f27054894664ffdcbe635e34cfa800912b59fdaa624b36c44c9ff4f193d3be2f97a7820a6d4ceabe967091ef672098baf82dd3b671cac4fd4f4b14e4ee388fbdaafb4dab2385df4fca23a78d31f11bca15eedd7cac778484258778106a07" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "add6c89153c4c0eead03df44487742a0" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024128_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "0a05baee927bf23dd2f4b57b90fb6434" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "8147e99dc9e462efea9c1d7f30bdf45c" );
            add_len = unhexify( add_str, "6424ca7fbf24c6c3b0b5eb9d769b26a9792c96a8585dc596208ae6cfc0b265bd8d26af31027f278bb92a9e3b365beae8d964ec7a4096513f84fa73f8739fa7e11d54d678bed19546d2b71b3d0166b25b47ad7cfa69d74057d889258a796a65f2bf8d3bb151f4e721d398e74594a186e6182c16fe4c8813dfec67215b3c4a94c0" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "05fac5520a99ad7fb407c48995a2c331" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024128_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "e28c435211743a7872e4a0bd7602336a" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "2ddbee94fcbfacea080ded468f67180c" );
            add_len = unhexify( add_str, "63190ef542656cc2b69a9b0daf8dbd2d38cd75f17b92d6d891c17b0337ad4fe4539d9154722fa430782a1d79620e974661918166e39c453c5a98759a13d2766138c7750e6cbdc7b6d7cbe44f3f4de7bb562d9bce6e6e2e815444842b89ba8b73454218c483e574ca886a84e8c9aa6f56dd1541a7e35a4a5b8f6a05ad5bb013e9" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "2ce6d74cda466354a736636bf18acfc0" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024120_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "2b2bec16c7d326a35a8e4c0b8c2e3674" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "4573eb54491ed91bfa2185b762115bc8" );
            add_len = unhexify( add_str, "7a4a6b3114dabc50b201472c5cb13a79430f78eedb2ba8492c01ce10a74d08565b9bf9874bb8fb72f694a23babdd08684cb68d7e09e65813728aaa5c41f9c2b10d921f8271e200e0c519c7c46f572bc9fe3f27e13d1e6d7bda4bd66c1c4b0fec8c68a1b0ed7b0659009dc894ad55e0712ddd0837315734f2bc3b757241af35ba" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "5f5d4695795b8580b0bc414a81b002" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024120_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "886fb12554b075dd9663efd076acbe56" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "7e7a73542868fc27a01865c3aa635ad5" );
            add_len = unhexify( add_str, "cb25c2f029c7a877a0aa565c7f7347b317ad534821edeeea838996dfc42b13787e5bb237525ac926ca8a6c5078210f4a27863e8114c728d09653fa93ae990e99f0c856bc8097c2cd33cdca1a407897e2f495d2e75356aabd891702f25ff20e6b6c8a785d74b78a734e311fd236f9e970202674004ee4151879d59340b20aa23b" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "8255116ee1e3cf936633017c4dec3a" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024120_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "920fdf4b39c63947d57a07eabbf3f2f5" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "77431ebaad53e42ca7eead0d45e5bd18" );
            add_len = unhexify( add_str, "11f82f9ef7c2161ba73cf7da82c5397da5e8278da180a976f43222402e983b057171f793641a8343d6366d6cc9260dfe8becb8396b5bcfa0f46908bd809bdab61126cbb8d63f601965fb9e4b3afd66c594dfd394d4cf06f79f361771a85dcead6f45dc7df10fa434736eb109a76fe6cda32c5773d4db6449494f2a3f6c884bfe" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "1291cbea1a9f8b166c7306ff9eb281" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024112_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "114060534f526895f30dfb4007356ea7" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "5ed7fb59618ec3d081e60d8259a3f184" );
            add_len = unhexify( add_str, "a56566a98d9d4fdcebc932adc405e0b8190d537f931983168283d0431e7589333d42f2a3d6e41f268e7b566cf48694cdcfe01fbb9198804ad39e7d387039575c5de787610a23ec265505a448c3a64ddac1b0d8c567eefe5c3c2dc1bb15af45b4bd8fc2e1506ddeb2e39e04f72fd24a64cbbbc929800e0687b53eb89b3049f271" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "62f770b3985388ac37e14e8d4696" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024112_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "697ca4e9de580b525d7149e8b69e8093" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "e844153734eaebd86983aa3bf50068df" );
            add_len = unhexify( add_str, "cedcd5ffeb7988837c38a0be4234ab1b03f14367a1a3854b6dc9f33eb9a87c411326e5cb7d12dc730cb6f363da2ba68affdfb651fe497942e0dd59668f56c23dae80b7bbf905d36b501ff037fcdffa472efa4bcc1c975b67e5d7f348db73e0ce648b44ecc5b5bbbdf3101bf32ea99e3c8e8991c94fa609c93d4b375a4389023b" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "95becb04cd39c868c9dbd1d4e59b" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024112_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "2fa92cc97ef469efeb2c25838193435a" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "07e6492f2377c04a85045d24940fbe8f" );
            add_len = unhexify( add_str, "0f021fb787c6de2be054bdb2741aef82ce35d951de2986c86c3dac77ee0804dfbd010d33a5dcc109769d4b8ff1471eb98fe917c7b0b374e80539f2f4432f92aa55d8398a71510c2acf85c54975fb09ff5638b936283efa3c1d3b054865f97685d6bfa0dfcffde3a20525b5324573b69dde230ea87c685e4f6b5c3c4c55828a86" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "397b2b0dad7f1926bfc25a3ba0ca" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024104_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "a61f8a5777ec3da0c3e257d421286696" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "14894cc4ff71e249f0053bbc1680331f" );
            add_len = unhexify( add_str, "9df46dde257054160854248e70625183bf957ecec36fa4f5a79a1650e04b500f7f2fab4bb873f0e813f0d6b17610bde0de95427a8e2d1293dcdde053f5b1a5a81af25d553289e89e77e4ad7d0a1190151724730149050bd021ec61a08ce2271390161c752df8b5f61c33ee39366de4c1db41d085ab9dd88e170e8c41c571e2cf" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "e062ab7984221ed226be353731" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024104_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "aa2d04f4f5258c6363b1210c91aff7d1" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "6b24c03273dcfd508cead2df0c65ef2d" );
            add_len = unhexify( add_str, "81a1b326f8f22bfecdf1f386bf8fe678a427e3886801b823a37860b9a832356724b1d352d6250cf8e8f89d0bf2314fd11464c3b4871478f0bc290ee1096c8f6cb5484176d70762289b44309d6a88e4750185abf30901bcf8d952da9abaaf9807c0c0ee8be2b247dbbfd182b83f9bfa67ca3bf448c3f5a3de3c31b058c3f944a9" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "80dee09fed5183d6405beeb268" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024104_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "cf221e6cade9f6cf509afa6979cc1fb9" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "d35433be41a259dfaf58aac1d82af462" );
            add_len = unhexify( add_str, "b31c477490e5624c4aac8e590725bfa8b3efca618e2369e9b980d6a463a014d55aa8317a9e70ce6de7c574cd15242cf4eb3eb078cd2f49fd82d1a56c6c4241342e62a2e9d94f0aaa024055cb441d650f0a6ecabfe9ef563d6bd87d4cb1bed348aee42487c13b73e52fb70f0ca6ed81924fd519806e04babfd08df1a00191caa1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "f1776b1ee7a3c49f99f34f582d" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102496_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "c98eb634c7caf52d3f3d9f344e141988" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "a0e58176826910a69c2d68ae1c6a05c0" );
            add_len = unhexify( add_str, "6e559278bc469cc670c4d9105c3c2f8fa308e11b4a60f75664a9bfaff4f0176175ddd3c6c17ff91a208dbbc7c49efff099fa873f60849ffaa3a3003419cadaa06b92a678b80bf6c952bbbe596dd0a2eed35507c55c48a9e6131bcbda0621cff87e02be5d082944f2c8e27211527717272839601b0e26cb5aa2301afd05ae1b35" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "3d8617b2db536ba7d367013c" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102496_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "c5018f4a8e2a850979b006d0498dd0fe" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "75e4bebdd170159cff59f895ebdeb118" );
            add_len = unhexify( add_str, "25ed2831fef205690381c73e925ef7ba20d5f2e3a4b5d7beabd749fafa08a6941acb1385aed977ea824322d378649f646a812e6c87ded6ae437c68ffdd4fae937a8498ae825d7523746730af84d56380be8f575c60e7f836a862343916e98cc2aa5a27cd63cd92df63b8bb47c81fa6a53740a125bb9cbb247c916363e60f5f65" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "0aa5aced93e0237bea9a0015" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102496_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "cefd40aeac28fbea6e3343a125fe1c9a" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "324b9722166edc3831bd19c1db5bfbf2" );
            add_len = unhexify( add_str, "72b7a4289bf7f5a752665839adde8f79644424839db059ce40de326414c09691d5c7071e43722104a94e430e263bc974b98f167c50b97490bcd4286b502f607ddcec5387695463154bd9598ce8ffb6104d1f7010bc196ea2dcbfbf452d6257b1da00271fe1e6fb56c43656d5570b965e0369502443536cc46d4c05b1e863ed8f" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "0c6b28de22e02fe6a4595d5f" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102464_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "58cb7cb58518ff3fecea4b44ad9fdef1" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "fe619efb1c9502c03cb8a70792f9e046" );
            add_len = unhexify( add_str, "1a7c444a84267f52c36f3c09f8c4a88b6ffe3309b8edaad93a08d3961af28b7c2baba5165f0a9efe13fa6a0ac595da156741dc7f728c11edbd8ab02f03e45716be504778a75374ee882af488bfbc6cdd58fd81d3ac5f369f85ba42c6fd7f9df4b25fdd2fd32607ea800047e06058388c4f71a5eb4d825e8578106041c84c25a1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "8243f32002d33cdd" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102464_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "15cc4cb979a343f4adfb821d6f6e9c66" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "68464e7eb64360c7c0a8540ac3473513" );
            add_len = unhexify( add_str, "d69f4a9595a48a50ec33ac1848df3d994eff838b28ea7c8b2c42876dadd60a3f9769bd4f61d8007c9dd4fde55edcec8f5ac3bf23b1a958fa714dd88cd5261edb69b7b086ef0f442179943f0871a6253aae99d31fdca448bc3efef353b5cc55cfc576e4a7fb73a5ab6b5af58dbd381bf7f9d69a5c2bfc902901fd485967b23bd9" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "c0f4302d8276c3d3" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102464_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "6398de910ff8f3acdc2217811a1da2a1" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "fc69b21ec18195901ffa62260fa20454" );
            add_len = unhexify( add_str, "021f225240cc9a68c4886824d373f3a70fa32b3a926c78164642450287d269d39dbd49c8c71ce7b914f83e8b53bc61c6773f98318557b45f0cc2ef2539939df7a1e6765117f75631dc5640291d20e6402d22cd2e231f9c2c67cb24ab5d8a69933c49b89c9fb2ea57136a6bf1bffe8e04d8d6c813040215f051c654d93224edfc" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "314d1a332d3c590b" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102432_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "382d86868ccd08d417d94f3b73729e09" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "069069c377958235171437b34e0fce76" );
            add_len = unhexify( add_str, "049af372e34ef7a92d0d49cf2dd03052dabacf2982eae6a817e6146ad799971be239ef5810ec3f6cc6990e9641a7b696392ad3faee38bb50746c1e93913c02dbbcbc6bf54f0d062f176779b7c0dd5d7ec7752601c9812fa80508a78bbd26922bed4f64b1ff2a8340ce1c01e317e3526cd8218ac24af87b07f8792849f6479b8e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "ffa59fa2" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102432_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "21052b2fc7bc7a662aa9dc4b6a04f25d" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "d7e5432def6a24d486a608e5c5c919a8" );
            add_len = unhexify( add_str, "1970ed40003bccabf7f3c57bbe5ba27e4254c1511413ed421cef3a6ffb9f0192987de83ae965478c3e9979637f8b3fa5d10d69b916f03fdc92ace7736f171660156d880114aefdcc164adb6f8c03940d9b43ce8881441b41cafee3351a56fcb632aa4b09ea81adea26fb0d8c6e1ae380df922a429ae1f5b82b38d9bda4323c51" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "ff342f4b" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102432_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "b6c53aa91a115db64653016375bd747e" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "8163a4fd9c2c7010bc85c86177b194ab" );
            add_len = unhexify( add_str, "93cddd318b999262c7cde2838cb5c4d78f3eb1e78d305e5f808fa5613526d724e84a0188ff42a2c34bdf3b5fff70e82b3c30346e179fb3faf378bc4e207e335a44da53a5ae33770104b95397fb5acb746e6418d0dfc7368b035af53b470fc66bd0c210b68ce1b276820b621e919f044e5cff5ced7e07dbb8825bca6b4ddd8ee2" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "50b8acce" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240128_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "2251815f5bdfe1111c7f9ca246662f93" );
            pt_len = unhexify( src_str, "2247e781763edb1349db2cda53e5853b726c697b34497761373c3b6a1c44939207e570e14ea94bd5f9bf9b79de9cafedeabc9241e9147453648071f2240e10488c6e3d7077750a6f7ede235d44c5a96392778ec51f8aeb1a17fabe9b6c95fbc479fff954a676813ad3d2f71c76b9d096a0527f2e1b151aa8972147582c0fd2bf" );
            iv_len = unhexify( iv_str, "58973280c2a7122ddfcb25eb33e7270c" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "b202eb243338849600e2feba7f25a05fe98323bd7cb721ac49d5a8136422564391462439fd92caad95fc8cdcaa9a797e1df3ef6ba7af6c761ceaf8922436dd5c8b1b257f801c40914c1331deb274c58eed102fd5fa63161c697e63dc9dfe60bd83cea885d241983a7e5f0d6a8fd02762084d52bf88ec35f156934e53dffc0395" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "c3701ce3284d08145ad8c6d48e4ced8c" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240128_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "3199b70e7115c74e3aa3745c18fce8d1" );
            pt_len = unhexify( src_str, "4fa0b090652d5a8dcd9b5f2ceaaa2dc87a40b30e2d59bdff09e1f204d1b90371de70935c385cf5b4d7e0c4e88661f418705370b901b97bf199b366e669bc727882d4aedf8171a8c39431f11af830358cd0d9e110da1a0cc6ef70efb255efdac1dc61e722a2d8b7fb4cd752c6350d558ae1ccd1c89f8ba44ab697df96681ee301" );
            iv_len = unhexify( iv_str, "808a019f7fb761e9701c0c4f1a1690e4" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "8d5ed4146fb491db9456e92f753aa4f688a9bc276e6aebb782a0cdf7fe578d74ca3946fa7b7893eff6345e64251cb1b146442acb64041324e2847481fd4388b17f83206948e67c1e66b894d5d40ecac0bbe4db0c6f58b65a1f19f29429a9e76f78ef5dba0c94d88dfc06e6222a506f004d24cdb3fe26d6eb6e08e4fdf6289651" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "908806d668451d849ba0268523eb0e4a" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240128_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "63805cef84ca7fcf281b226c3ae37230" );
            pt_len = unhexify( src_str, "543fd64d1454ef6c007ee96b3ff5d2e4b7f5d15c23e7548dfd1dfad4da7774b8795e817fab3be7fbf8e4d0d351a743ea793d9d01385a552f78ede054be079aebd1511013de2096456e9fc1b83457fa1240cd39c17440d4b55c4e390119a759055ac851a02ea481eb83e294922d35f687a56d801eed638d289350e141116ffba8" );
            iv_len = unhexify( iv_str, "1aa9e75d7854509a85d995ee482b8eca" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "98db9e8e3ff23f09e585e5326f525e4f8350a1f233a0aebd60d5951583eaf5220f1690ee3607ba98cf8cc99a90efb7197835957f2bda918a32e528f55d548e3c83d65910b956634224cd5415ff0332c165d1241f7a93976649ebed2cc7e62addb76231bb738ee8a291b62365965392aeb72acc5f0fbd2f88f5613fcf44a1b074" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "9b1baa0b318e1f6e953a9f90b21cd914" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240120_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "2ec9245e8f567e1cc8795bbf72f2999b" );
            pt_len = unhexify( src_str, "f266d0060d290339def5f6d8dbf7d120a4c645aa90470e168b4f35342a00b8c7b7230003657d377d8568d252765df142e97a9dbfb9711d9ccf396f3d51bd91673f129d58efd80ab83a0678303e29a0dbeb1fa9fdb7fbde586a17ace65e894374ec8da1ccd3e21851ab998534de46cb43b38e241edc04b5c571dfc0aa0074d4fa" );
            iv_len = unhexify( iv_str, "413628d9ff3e4067d840b0abc2cda0eb" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "145d83092a269c8afea604e9192b8bb550b9bea85f842fcc4997c2b00c6f3ca46100e814e82389f27a69a12d29340c5827e607657a00fc72c4de30079e23760769e800ee4ce46957f82d61935d07d1c70dca836c19969dfd0fe0ea740a52e2d09b1c9aa137b5e8527756fb2c2298f8400949ba24a8351c1093626723a68a79f5" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "ad174d1edc713c187a5859a390fff8" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240120_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "b08df4acd253f9dd4abc52c4be488015" );
            pt_len = unhexify( src_str, "82f665910d853fd2b775bf66a1707935443574c90483fc33ba02d6479fafd99c5f816bc58a1393a44fb32711fbeb0d6936efeb3580f147c3019e9f2e2ef48b202bdd369c277791bce524f3b22ceb74c664143c4b1da819b229a5b480aa954be110ca006615d9cff5a158342a47cb6d04fbb817ae4ddff6d4f86b74205799c9c0" );
            iv_len = unhexify( iv_str, "e1c27d35520ea527f9a2cd9b0f717841" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "f5b0fcd812061be999901595b3547e70f7144cc9e0b0098262be4c440e8637af782f536f571534a658ad1fb44360d9c454d1000d6957f261401e09c0f19f5146ee5433e378423f9c94a90af2185d38cbe2940a459d8409d987d04a1f3e686c2b91d4fae1f3e3bdc5a30569838201b7d30c7320d7cbd787bfd6cd40e7e2d071a1" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "fa31e58fa32d1208dd8a67fed44033" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240120_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "9c08d6efb167beb035f71554f64c12cd" );
            pt_len = unhexify( src_str, "704f59d5202108b949170532ac1e78edb0e06fa323c1c69202d7d22dea4d7342199cebe949e980a21ff0fac282b868cc31ff4f6674c393c0f2cae2374664314afaf7791974b6bd6af26ade7fc266a6cd2de4f3c1f479f895ff597998cc8b929c1f05db13d9b9a4d98c9bc606eee32915bbdaeec6576e1fa6e8b22e0bb1098074" );
            iv_len = unhexify( iv_str, "608d56f6dea2fdf175eae189d42a85fb" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "2c7d2618808adcf8edf5a54119471b930e07488d5fac3dcb53f4ade43674d162881bee1f27dea6d158b254d4b432e17f211515bf595a9874d89f8cf748ddaf2324078029c6463312ad32eb0aa5ebefc31c7fbfd04b37ba6b766375952c211d160b943e9d3c5e144b581157bff9071d31cfc082b55c4a0fced386ef2fc75e1a7b" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "7a1ae03e2838294e286dca4fbbd9f1" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240112_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "192dbfdf86e48bf18710e706dc90e356" );
            pt_len = unhexify( src_str, "1d7c45c8ef6f9f073c7f186e4c876c2b8fbf22feeecdc111a19071f276e838ab0572c9a68e9ad464fa88ba8d8a162e9f5ee1c4983395a890990357673467988c057eb8a0342c41867baab41456edc3932531d1c4aa0b42ce2b388d2be579dfe332f40a9b864c5e33e2b3cfd73b68d65c4db9ec46d3ba1587a56cb7887dcb3c5e" );
            iv_len = unhexify( iv_str, "1a511f85e0e138f4241882c20689f881" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "3e50e821fbf83433155de7b4eb3c9a2c148b08d9d3998a3486f517fb5d0a1338faabbf95e85fa9186385bcb9e26aaa5e473d3cc7af869872e4fb36ad16c5468d994e9c71a09dd2868977f3f9064664f6ffcbac1bd313a7803c304273d69ad20369bad36adeb38480563bc6db9aa0d11a0e03d09731171c1229a756037b2c285c" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "9393edf0934796eb97a8c513bbfc" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240112_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "daf9455bad8bee905c6cd464677b803f" );
            pt_len = unhexify( src_str, "af04226cc6eb84f8167a68c2cfde33a1521dcbe781e7b97a3fae732bcd8c0616a588200328902faa5a65a27e769a720d7ea23333cc1c66c4d4e4c53facca5d6af06aea7fb49b12b04cd6ae38fe28d71cd66f769d640beeb07f508a0e3f856902cbfde6919077de378cf0486cf177f897cd0a56b69db3a31b448ebbf8fdf63736" );
            iv_len = unhexify( iv_str, "6cfe8490e892f5ddba8bbd1cd522ba0b" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "e5622ca7360272a33e30f7fbeaa00956e8af0d871c433c070c8854d818eab9717293e845106770ec07da372c75266239a225ad74465e255520218c6736e51070477d70976aa7d449c32a5c85bbd6931c76e9e4355f9697bad2ea3bcc0be005da15c62db219b074b71fe4a5512157143df2c1f70bb17c6d3740d8d20eef88535f" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "25fe6c9b2303b40ed31d1beea39a" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240112_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "82d166dddcbf4f7f66aa5ac6b12516bc" );
            pt_len = unhexify( src_str, "7883f4f96c0ef7f6d9fd7c2eaad25995943078559eb24a3e6650126ddaa32301b04f737dc27b648d6115ce08feac862cb888073b22aa648c752934bb7f9c566209a97499236f782758d6f6f9a012a2fb6885ca91858f9779cc93950baa731f1874629351e6186935475a20593f66cddefff89be0fc0f9b57695b147d9acd8157" );
            iv_len = unhexify( iv_str, "540c2a07689bf314bc8ede71df3f4358" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "44806e76a40bbbc2de860cd36e93d64c9f4c11994f754db6a279d6eaecfdf19966512de5223d8332a407381114d50fadb03e33e347a5f4d87c3fbf35f2d5967ba295003a2c6c12fba8394aa5b7a31365791c630734a6b2ef84eed0738cb4bc229e93c4e8529aaeadecff7ab93887b9fad5f05a88a5ba9fb449053ce4c6375d1f" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "756d65c1b8a04485c3944e2a3cbc" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240104_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "81c1fca371968513a68ac09a7459042d" );
            pt_len = unhexify( src_str, "182cb89c94171b685016bad76c445cc4561aff8e3170dd251f62efbd44910ddf8eba8a67dd1a237f2f7336f436edcfbdf9928e94c3488189110d672488c6c4e0dc4a1fb6e67dee9a1bfc3f49d2f934f305f139e98f0ba9c1ab56b5ce9ddce4ab54b6970bf6499e5e825abbb23f9e320ee05aaf0d712c09b0134839c5609e178a" );
            iv_len = unhexify( iv_str, "7c962a92b8daa294b4962cc3020dcd0b" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "f91e36c79db6789a3acec9e82ec777efc1958e7e5634d30a60239eb7cae1b48f40557965e8a6f6993db3f4ae443ba167753c89f52f610ab69159ff60233310c1bb2baccb936433270f8839758bc85c53604e771e3ab0df6d6bb02e860d0eb27f425c7d30fb7566aff982d289228da5ce5a45842e10ffbe9016c9e926d7f69863" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "0114c2de8f733fc18f203150a0" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240104_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "09ce73e733e880c6d7be92be3144db40" );
            pt_len = unhexify( src_str, "a283e20adb6efedc5530f4efd71840d5fe61c902a7511cdaa939f5030880f3675959ee96e39abe082a66eba2a5a93214b22c249d7167b7a0fda360d02df855d508c7ebae7016137e54290904909b2d41a59942abec76612b17ea76ffd1ee715aa2b05b1314c0ab28631f3934d0e9efe2aef0c711e75a5c62701b3358a414958d" );
            iv_len = unhexify( iv_str, "f72a2fc910fdeeefe8743f57290e80af" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "fe9a7f59abc3720706c33fa40e106663d26c0f8da0d25deb90ada8130b6f95aaec07f4a7db342b678d102b2c81464e4ca9458732783cdc3a9d504232f44e2878b0aaeec0f88efa5d7e5fb146911dcdb4569de7f114e1854ad7a95894561bd0fc4d9a5b58b5164872833283ed88fdb4900b2a596db4e8379eed4e3a5c08d5fadf" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "9de97bfec1325936bd171c996a" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240104_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "e61d415db78d9f2695344350e0a8291e" );
            pt_len = unhexify( src_str, "730c3fa9e07eea73a734b17fcbc5a969dc2c04f448f44c7f6276e32ae3504e9b15fb664908f530e83a74e25a4525f74d315ab85d7b85005401370dc50fdb86e97baf3e7acb403e476193527a1a5d642ffad6cf2555d16d28cf4c4127189056389368b76aea806906b0a38b808cb02378eea48edc005cf2c21e6547502e31d2cb" );
            iv_len = unhexify( iv_str, "e09dee93466a3f35605b647d16b48452" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "ae87e754c1af1175b474b0718e3560240f55194d946d101e7c0bc7af18d90a50fa41d68516e45dc2a4dba48d457ebff18a657a873e15620ed7cf6ed3a26195b9d354ea279b24ec7802e4e95d3f3765188a64d7b8d4b7c215e7d67385efc6288724a33a1a7994f21e0dc2970076af7cf31e9ad1098537543052a2b0f62e4e8a87" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "5de3c5716735d7d1b859debb6e" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024096_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "19bf00b228ddb6e8f1fa4ba85f866475" );
            pt_len = unhexify( src_str, "10742aeda590024bac2696af8402580d2ec6ba3f51cc6f79b6cfbb3057634ced6033fa43dbaec9af8ce7e9706ca699ede88d89caed89ea023d14761bec49da724538b4f9672163a5bb5dbf92f5278fc0014eafce402cb408a1eaad6bc17ec0e835d6b80f4701f946661757b9b2d54d1b137841519dd38d72835893ea6d52a27f" );
            iv_len = unhexify( iv_str, "760c5b929ac3d33bee4dae0088a894f9" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "b03d27bc7f4c9d48d555a38091347f371d0522ad4c347b4a23194c234c7877cd3621ce5a7c2fc26b38c7e6f1c2bf228ccec491f5bc352556c08e4e19ddc4e4b2c036f45a42aa425a5ff9a2e9c9e5580b538ee56fa804a86d9b1b59b6fb0d00216a96936755462979dc14990935919026fb51cdfef05b8dad03320a8112b7ada5" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "2f1cc79408c85a9867214061" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024096_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "65bd9e7d9009dd6110dca657ccfe603e" );
            pt_len = unhexify( src_str, "c1b539324a001901c2461b9747f605a2f4043b9b0f54d1357049fd1819de06df6e29880d62ef7d91f9cdd1108f3cce323f6c32cec16f7bd434e539fd00ada476ef41efe7c6907ad1cb726717ab56d6e2d32042ee2df3f90d15e1515f0a15a5f06703e06e14229d18328116148b3cc39683918e42927f62aec49ee9bcc19be38d" );
            iv_len = unhexify( iv_str, "3fddf7e943326e431be540c49bb917c6" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "2813d6eef070cbdee9d5d71caa8a88c631f0b71c41813c6219a765e4fb3e6eff9afe8f8f4394fbd5646fe80bab78806eddf7549d6ca3d0d16d47ef63db93cb5620e3814efd86be151b338ee6e2c681bd37be4039b2ea4a190feccd7d65cbd56ebda81f4b66ce12cc3e2cece731c37d4237a9dd0a2c1a7697bae42176a673d62a" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "96200bd3e64d5eea746693ba" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024096_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "b9b8ac9215289aa003cecd53a90e0407" );
            pt_len = unhexify( src_str, "8a6fbd067144b6d50ea73a2a7abba3ee9677bbf00312c70d808fd124541ab936229d59842c8846569a063fecb8bd1945882abd987a936991d5cdbec087937f91c4f5513feffa1984a6b8d04a7b69eb4e93e90b6825778cd2ce9a0ce54d4a468c93884619f851d2294be0bbbeef5fc0c05d2384126289283d5ddaaccd89711d73" );
            iv_len = unhexify( iv_str, "27d367f3f0c60acf921f8d8b228a0b2f" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "42d98ecfb4f707ec233c7f990b0cad8f39546b861b11d8cb9d939b29ff5ab315229d946ff55927dbde82c03aa73fd7857b2ad38fa55a827dda54d2726bcee66347ce42c9cfd13ba1507d209ff2388c0ea2474e17e31d8056593b722d3c2a302a716a288592b0a36547c7fd47f7595fee9d30f5bc09a9555d7f3169e26a924db1" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "d66974c95917ae1bf79b6685" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024064_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "ccbcc39512425bc32350587f0fc3e8fd" );
            pt_len = unhexify( src_str, "57d6ccda317b7ea150b18d9558b39fd78d9cb52509aa5c095c5b46da89b79918c85d469ffac7226caddd670ac8f5add47fc382df1f32b4de9cc1b2ca7c2acfbdcaa08429b97e77eedea55c8ddc7814fe4c3cc1e21f95d94301ab77b4df7572d0b8778cb2befc0f4c4a5e93429ad52d6c2a75481f38d92edb1dac563154bf90b2" );
            iv_len = unhexify( iv_str, "0862ebfeb40ff24bfc65d3cc600f2897" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "e6a77e90750cf0e4c276c50c3880b3f6fa357179cbd84e22f5b43cd10abcbe04b43f191ed3fabf83eaca886f4a7f48490fb1fd92ebdacb68c5158e9f81243f7cadc7a8ba39721df68dbf2406fcb5dab823202ceea7112e5d25952de1b922beda271e7677421fde25f8cde450c40667387e5abf8da42dfe891c52bdd9f5060dba" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "927d13cb90ee5f44" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024064_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "396b53a694b28b717c104111c4752074" );
            pt_len = unhexify( src_str, "bbc3b818f4ff10b6822ea41f63ca53c27578a8126f5163a5014c60e1bc8c1a9bba67a3808c8aeee09ba9e584a3584e9b86895a3f0db2e64e71bb18b843b12f4ebbfaa1dff3734196f70c5a6d970277ab5337e8b940ae7c957646f8e96c6b5d84e9e97b620a926e655850d09bc2d94678704aa45d1788e7c23ecf37e2904a0786" );
            iv_len = unhexify( iv_str, "0981a151c6f6867d3830c1f9ef99c433" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "72a5587076a1050b2b514f047ccdf7176c118db9236c0f72091513da39d7416734ac50e0a35b2905420214be8426a36e86863c9957693292bfc5bfc2e93d234a09e80f517edb7cf8e5d21d5ae6c2362b779a9b62b4c66202894d369d219ef0e4b52a342b71f248c18ffc345dc7eb0b47b3bc83ffdef921eb42b6d51abd889ef4" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "af99f8797495dd16" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024064_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "af090618cb454324a82a75a91944dd6f" );
            pt_len = unhexify( src_str, "3ebca6ff138c527b851b27b9e3917bb9a07282197868351dd599b74b332610bd634422911393171305caa4fe3f6e89ab6c033ca759e118c2d8684b903966999125c748e04312ecd2c1ac3135c3be2df9c8c67be4d8303ac7aa6c21ca7b7c20b1108f5622d8e6079f41e4be4abda99f782ad35a085b7db83482dc71b8e5d8e71c" );
            iv_len = unhexify( iv_str, "3380a6f20875b7d561c4a137519cccd3" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "6be8eebe7af78c062812513785e9803f302c771e8215e4c606fc5eddc3efd8b12c96e029b4287da55d8626583e58ce0e50c4ac5a39a1b0f309d5803386738397376c0ae155087f36fd86fdda4b5c8dd079011fa9a134ca8a76de570ef165b20d7d803544cd2f3a0ffede9b35ca1c982978bf95ac100af755553fdac38d988fe9" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "3e869dcac087aa6c" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024032_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "041cae51d9e631ef70115be58f8818ef" );
            pt_len = unhexify( src_str, "f6748f4a261d876e37fe44a419cfe965888aa5ee195ae12237322f6e7ac4bfaaf16e8e29be507e2978339a1855ab918485011fd52f834bf0876ba8d89dfc01927e0930d03c0ac7dc7ba1554a879a2051011bcb34a5e4c7cea4d4fb5ed53b41ec8d17bd52b2e1b9dd417a84ac5913ce3f9fb04daf4d14be65f49d0767b9431b47" );
            iv_len = unhexify( iv_str, "c32f227659e0566faa09eb72d99f89c2" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "f30fe6c8765c8c0af579c95bc2d182ccc346e587a57aa226eafb692675377a85e9ee08339a047b9cb674dabf5a25301d2c8c264bc06573e36e55ceaee39239e367b8f1a3d781a2020e548001f9f98850994c3aa79b13dfc93c1d7291befd91e044b2f5d2583d1a9f868fab4afecd46fec7d315b0cbf8a7331ef8f588d75f97e2" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "5629e1a4" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024032_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "f0577d9a7dbf7b4ada5b9758eec4c847" );
            pt_len = unhexify( src_str, "5b559738634825921b5cb620b5b9f637f8b7ce33998cce1ed1a23ff01f84e58255d852a02e59e4394752405ecc15248f7616a33e64936f726de6fc6d10c3fce9ac0b3fcffbd755f16bff8462b3be24f7cf342c8d0bf1ca79b1cb4ea88d690644998a8ac3cafc8c18c8cb737e38a681026d46966b89c7d6c7a4ce7a1e1faecdd5" );
            iv_len = unhexify( iv_str, "b432473ae67205bc7a99f5ab2a2721e6" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "ddfe664e28c5face3761deda1ab2dac6e36cfed538e3faf9d79c54e3c85b4baea9eedcef7f8f28c2feedec72ab2cc6aaae101b99512ef18e759b7828364e4daf9a572f8c6ad88eb82f7304989345aa4985e498dfebc58cbc45aa31c18c0dda5b1991fd998901c65807c8cff6058b1d5dfd583297da8451cef13f246547ad11df" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "ce55ac00" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024032_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "6ca1d6ae9b5ddd6e3d68656c508df318" );
            pt_len = unhexify( src_str, "d160740aed955e30c1f946088b5bc5bbaf5c84f282c32f65d099509993628ba5a51b411c6ebf57d58e9176b490ab90fa8db8a3cdc67a5f8322d06d719d91f00ca07aa2a3977dd0838487f2e9d4dd285067a1f72bb8a6c9dfca107acf1f404995bb68ed9d7e12423efe570f144e0533fa34b8d0b7156112b85c94a8fa33d7a6d9" );
            iv_len = unhexify( iv_str, "68a494c9002dadf4f0303dd0ebd600c0" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "276e362cb73b405b10a98731333f6accf0d19cb96c21419d6d56b30dcf73f7208906b0e3eb103b721cdbb7eb1d4ff29ec3b7e9d433205bd9ec48c59d0075a1507ddf09275426c0ce9a58b973e06d6fceee7054ba92b1df771011ac73e39e451d9ac3375c595631090a2296d423e3ef806ac20770abf78ad04114f65661804fae" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "8ff9a26e" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024128_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "5a3e577743b4581519b84b7538fb32e7" );
            pt_len = unhexify( src_str, "172a0a14820448e5ffd017c18ee02219906f721c915c4f0ff13b7b7889812c0edb89f28be0c22deff76bc975d1ef8ef3fc40b10cce0d78933aa22e6adf2d4b7ee4ed6ef487eaddb666afd8671427f7525eb99af54a55d98159fc5d651266c65ccd915cbba60fb6e2c408ef177d682253c0b5410d77d08be1d8f175ca360becd0" );
            iv_len = unhexify( iv_str, "1e155ada52e250cee145d69b4a307bc0" );
            add_len = unhexify( add_str, "b9be2145b842d2f5c3d15ac032010400bffe31856441cb484d5c93e6710194b13e14077e132cfe03985d4b936bda9383c22c392968c748f7265213a8eac584aaa11eea35589e3536e39b3e4418248927fa9fcc027c5516e402445068ef793d349eb778b77fb0b37f51bfcc3c21df9999ca9985cc5bec6502445b068c2d061f41" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "b5bd224140d6b826062e55754299a43a87cbe861360334897e82b7a6023ab0041736479c9aaca7c73f27e239a63e7433e048a8d2c2d26f0b18476aca7ac20837affacdffb57c618ce5982ba61fe1792c8a3a856970c095b0c4695dce961a354135075e0a786192d5875d16793a3ad0e3572a81efa24099f5ed9c92df55c15dd1" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "74df58fd4a2a68657ce35a3ef11a9c0b" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024128_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "deb0ab6e8b0f392af6b89d253e923f1a" );
            pt_len = unhexify( src_str, "14a86c431bde5c0861e6bd2cb748a13b9bfb2a4a67a0bcf067960b3a9c7a75fc7ea321863c83693c70076462ec3179f4d82ed4a1155a4b5004842fb47482bd6a83804a05af2504f6f535eb9bdc95a9a2eb80c7dcd7dff54e3c00437e4da9c433c88f6d248e4754656acdf8ea7d68106b04ebb2f1cdb247fddb0bca1f8e9ed6a5" );
            iv_len = unhexify( iv_str, "c1bc587c3440f1f5dea5b0a4b5ee8dfd" );
            add_len = unhexify( add_str, "602cfb09e8bf250c3a2c248c4e91234629a4fe9a18c5f8b59df215e97dd873a7c1204bd0695796908daa28b77353e0e5b37877a7441d35633119c0aee9aa82c3c18a7f577d09293fafce1895dafea42f97222a33b001907b978f11471cc0adc46243e8f7fce94803d4d0595bc9fccb9b9396b52deb943280eac2c4eda54841bc" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "a72d27136d0b4efc0aa2126a246ae4946e2c62cf5055f7bde263e7516ace2b7e12179980f8dcff18dc4fcd662f38d3b9dc7f8a057827ebf27e5dab85264d9325e0eea3b12f8e9e39ad686263df75b0758cc8af0be89882bb159c95b8de392b3e295c039a520d2e56b50a6370afa57adc967f7e4ff670dab471a57fb6c81401eb" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "eb26cdf879e0cb1320d786a642c4dfc0" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024128_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "adf6006fb1cfea0f9641a4c35b864101" );
            pt_len = unhexify( src_str, "d21777e1fab632bffd82a58cb732794f112cd88bdda5a7a8d19c68ace343fd786e5e512013887105c21299f2d6ae23cae4f03047c68f019d98e76d2aa1b3a204f13f4cba13f5a8957b9aa3ebb44b8024b26cb6139a3bca3ada0520a68b8571ae89501b212a1f8ede5753d557ad2f38d9465dbb09b555300b13194bf7817321f7" );
            iv_len = unhexify( iv_str, "a349d97fc677d8ba6f72e8cc7191ab78" );
            add_len = unhexify( add_str, "5717bee8b31640f3999efda463d4b604c1cef62fc0dcc856efb4c50a8c6b902019c663279e1bf66fb52d82f8570b9a314647f4b1ed86eb89f4be8981225f94d4285f5ca9167434a1569b520b071ee4448d08cb8623b4cda6d1f7ad28e51a2df980b5a999025e9ba646707075a6cb2464c2a0d5fc804c98a79946fae0b4fa61fd" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "345af0d804490586c9ffbada0404176f4cb1331fc77705175619f27d107512d3e6068323b276743284feb938c5718a5b013305fb42282a89e270d24585236fa18265dc7e8ddd2b3efe93a2ea05ab359323c75211f2133aa97022c9a937a467af37c92a795c682a30f2ba1c4ab2dc45e63c56cd3b29b0efac2caa3150e6a72aa3" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "ae7d2827c4f1422b728a9fd31d8d1918" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024120_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "97c83d4628b65d94341984bbc266dc7a" );
            pt_len = unhexify( src_str, "e998cc0b7677fa2e504994e99cf7bbd84ba7e356d7da178f8ff40dddc046c70554ddec1d28aa23f9c4e6fcb9effeb8e28a883ad05bd0a6041b8a24d0fceff200a4e33996e279cbf029b11d58185adeb5e5e797a74d0d8b17adcf06dfbe3ee11d8e6bc3b6a8434de6e0ddfa0fd08c913f9fb911cefca72bc3f616b4ac9821f53c" );
            iv_len = unhexify( iv_str, "671dcc5001c2146bf8a4e522ad702bd8" );
            add_len = unhexify( add_str, "9eb12a42d2ca06a7da37fbc23d213f5e3f5e15580f01b0ea80eb4b6bd283e307dec965745ea3b3509d3269cf25808fc6a923e97d87d0c1a30b447a5a27a06d0c88a96cd90d990bf208f1abc4934f6a0ae34a694750a74ffb27f4bb66bc799d43570b01897b98b00e6a01b95b356b11d33e852b2010da5785a691246d0be2bcfb" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "5a6d8930e473e292e67425748e8618569b7a478f1e183ba4e4a64385ac4b75d3d42b1afc34cc6daff341f10c1ad8f03d77179f52a7239ab3261f5fcd5a0b4282d26fa4d08bf0c8a5c96782c073ad63ad233dfe3aa0290a03d73de14d445b9ce4ea0e3b10a4aef71c5919969b7086353c942c479a1c052a749afde2325ef46f7f" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "b81cb7bfd0aaf22b7233bcfe363b95" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024120_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "2dcd5c974c5d78cde0d3a677d0b1acdc" );
            pt_len = unhexify( src_str, "21b61035ca3c149d66608d77edd9770411e0ef73a97d4be9dcde95ed7997ba97117ae6c1979195a5d916ff7a1d43ddced5287004fb60a2c81c82b5f7c8a336a603c3eb7cb160bbf21b454f810681450d65deb64e7cd229333fc5e85dc29040d7da48511b6b2524f02eaeab422b5ca817796c47b9f2d7d498abc619b2ce2912bf" );
            iv_len = unhexify( iv_str, "7455fea1bbbfe9479830d403e33c9d1c" );
            add_len = unhexify( add_str, "d684d38f2b12111197ca512c54c8e29ef1c3b9b089a6923cdb327c763f0ac8c2ec0900c716e211e7cba1d7c13a60fe87f5d78e5d5215d92e57a0645d9b2eab4b11870b5f7bfa9f2c9e4b9fcf7596e7719b7d0c0e6cc16efe71d8bc92e16a83d4782f08e9b97dc85a18c435b51c940189a3c2608379a21a8c46633020b9b6cd10" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "eb039d8cf0bf217e3f2aa529ba872c385f2770ede6ca4ed32fd22cd3fcbfddfb92d681f00df6fbf170a5dad71c9988d556cd74bc99e18a68683e0ea7b6ef90b21ff42cef8c4627e4051bff0da00054390e10036f430dbe217e5bd939295d9c9f64c2614d42ba62efe78763cc427027edbd0b7f72eceaa8b4776ba633f2c3d500" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "18e7b50fcec11c98fe5438a40a4164" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024120_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "e5b132bb7aca3e01105848f9b37ff516" );
            pt_len = unhexify( src_str, "3b6d1a432b7fdb4022fc35d6b79ea03b6aa14d4ddf60a160e976909ca069242fb2e7d414d4e34ffdf9416823c4b3f4e018ac8ca689446647eda6a12029f886bcc9d18be150b451d78fa72b9c4dc13314077a5b04cffeb167005c7e8379940e6b998316bef9bf8b5a742e337663c0ed91d88d09d0c3ebec37aecaeb8277b13661" );
            iv_len = unhexify( iv_str, "24c1ba77d37f99253576f4963779fd59" );
            add_len = unhexify( add_str, "dedf78f05957bde906639bd35eacd8fba8582d288c9f14a25eb851a0a34c82fd91f2b78614ff46ca17fe7781d155cc30f3a62764b0614d57c89fddfdd46af4fa5fc540b9ee9076805d4d121aa0dad2449d228f1fc3c07d466c051c06db6846b9012e8d268c6e1e336121d272ca70d965389a5382fbfec0a439e979f16fab0283" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "9976d2f3e16485b6b3699a541b6df386562b5ea4f6f9ff41d265b16e2d7d3c5f131bb5874cdffa87e704ae3cc24f1dccb62bababdcdedf8bac277a7277ca53a4d38fd31f9fc83f86a105663f045b70dabd553137b6d6222abb334b7be7689a4afa28103619f11b8b61aa92a63136ad5639f11bae64b25f09f1e2db701938fa5e" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "29d1b8a68472f2da27aa84be714108" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024112_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "63628519a0f010620cbae37f8ad34570" );
            pt_len = unhexify( src_str, "6db2919208b09a8abe5e95dcfe0f957dce1ae0e5b29f06bf321dc815ceca094f38c5c812f591aedbc9fc28cc0317bd1d89d4a3ba14f7b3e5fb2e03778990a6006e0ec2ceb47c923f3b17473f99521491a4cb2f9bd435e3133dc90e129ded9d15d78e75bfb3492458ce0964d5614508ef2a38ea02ec8664ba901891a7cc86a62b" );
            iv_len = unhexify( iv_str, "ce0ad75b94ab2d3918abf255c854ecf6" );
            add_len = unhexify( add_str, "c29384bd7cd013fa02487867595d739d99886a3bbed7fd5acd689f3a74f240f14c8fffd0bdea1f83bfef7b58ce512849e3a986f37afa54ddc11719169a49bd7e7138a745053417ff80cab1a32ae9be476ccb61ae055b319fdee5dcab629bb237aeb7d998ce36dd9c6908451c3bca9d3582f7fd60e69f6298d43a3b958341b611" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "6205d37d720cbb628dbd5069f38ded8e566030eadb7fbdf2ed827d5f5a0117a21c75ade89782b3dc4e7307d9a7ae406ead0145aea1b6cce286103a55ce195999214b84bc25281bd7fe511868a69944d483e05ea6b39b11558ab46a33d227734eb3a386e30d58c3029ef0cb4046c0856078d57a6df194aa8c0e10f9b6ed8fb40b" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "423fd542498825cc54501cb42b2c" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024112_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "7c0e1c6bde79315f79f22ebc77107228" );
            pt_len = unhexify( src_str, "9cd56b16aa4e130c3dbf30e701e8784ff39f866031e778e9ab72b858c3e333e9589b4b6cd89d6546e52a478d92bd59d0e4756d6b5037ab1873d88242ef31be643745d26395385b71034f6f0c0c84816f0c6755965fc8a7718f891d618f226684bcc77f87fe168e178b330d4b4c0eb4791028017fe6c42e68b0e195654a5d65e5" );
            iv_len = unhexify( iv_str, "9011dee57c3b8e112efa4d2b816cf189" );
            add_len = unhexify( add_str, "57bfcccc6f00c0abbc5f30589dbb47597838fdd50dd622eeedee33824e63ba78753c05d2543687f60dde501757b6fb74c17fe34b3e9c455eb38cf078c8c77eff68d3e3b8c244cde70ddf61703664d34159a11785cc6626eb1cad70ab94405616fff52c0f781ee6b43ef2a449924a76b762035ff479cd6006c21a62a56a14650f" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "2c1ef998747163104e5a7d2a440a1a1cc2c20446a9d0cf5f138f85c1f5afd90fdc3fa4932845c150518f40bfd56569a5479126c49061ef350b4fae895170b4eb94dad7b456890a822e1bcb57f9bde5bea747d17be3d18ea201cd99bc46fee21132c6918ffb0117744f6ba3f25bc8a50f9719854314b934c3a3230f4757a49113" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "4ef9aebb721dabe2d09101037a63" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024112_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "93f3fa85dbdb2784fb078a34b1116eb1" );
            pt_len = unhexify( src_str, "e7a0fafda0b90cada671f5e2adfd2e2a5f14e4613ea76aad57e79e2cb532f655210614e2036d7ac005ed5e516814d8667ed71e0f29b9c7b470f4722327407cd6ce6dbd298cee37bff33c35e34cdfebbbf33934673469d6b98becd6d26868977e69e06deee99c118fd4da3530d367d20d15107c03efe0d7e7b38710231e0dcdf0" );
            iv_len = unhexify( iv_str, "f5a7b0b26d1e86f4fc69f81c9eeff2cd" );
            add_len = unhexify( add_str, "3d2a1dadccc597b5e7b6ce48760150dee01c8550b525c587abcce8c2c7fb6291683a58c2e42e7b7ba6a3c2a117ddb7e67ea058a78989d67946fd9551e30fcb52618dcb9fae079ca56b74572d7b6a7b6a5c60e906e9639eac5ee1a5a2db864721119da2c4c5110c2b8d487e792cf6929600f1587cb2d48efe6864019afc32af6e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "60da3f4b3a263bc0178379646bce391bf552f60d2833261962375d2960c629dedac681d86f7915ea3cffdad0f37e409668f923d7c860525b994b325396531994a2fbb2d4e909d0b1dce322e078b4b8cd99820a39ffd7b468bd3e73b418b9a2cd5757b7d45f0363574c925bc22d66645abd95a6b29ea6366d8c2252d1c5710d45" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "833d2c55f5ee493060540d6b5349" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024104_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "163c05f69cdc4e518ff6445911d1ede0" );
            pt_len = unhexify( src_str, "84d8a1855423293de37ebfd9715a9b46b175bc6d44e94ac8a3e7d409e8a227a57a6b85144a8ee23564fadc28742b69e89c0d4aadf0a786f9a5d5f9198923643ffc0bfd0f96e43b08f1435d4afc0e49c0e2241d938780975bc7a31cdf38f30380753bdd66be72b4dff260a35dc10b9ba35059ba61b0beab16e35068721bd950e3" );
            iv_len = unhexify( iv_str, "4b16188249096682b88aa5e4a13f62c1" );
            add_len = unhexify( add_str, "a238d1111efb7811f6838c3cb6f3bf3e0ecee6d8efb26845391f8adb51e497e840ea40318bf8e3cf0681c3b69951c4f03d5a4b5edf7119a150eafe6dc16b68f3d2b91e1454637135148f4fec132bfd96ca088169a35961d4c663535b9852f12a00ec4c08082553a09ea046379ce747c717036154d063d876a2b95cd7bdb42daa" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "3bf751cf63bc1b433be6075303986ac1d0592dee400774d0bb7a9e72224417639e1e83e69f34226b873365f41fdac925628f32ed4b572b374310edfd892c5e0c3197e59efbc22ee11f0d4a66bd73a6f5b0de7c1cbb0612a63a262af51d418577a9bae0a8577e547382878f13047a92f51a867f8b7d283d2099c34c236918f718" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "0d778299c4dc0415ca789dd5b2" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024104_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "a2ff7cb9fe33b04a087d9ee6db58ec0e" );
            pt_len = unhexify( src_str, "ed7c22218009ceb5b322045fecc1fd748f27655397a09c2c29813eba9a5cbeebe88d4a35dfd741ef0ac1d11c4adbc6bfae824af88e3ce09f68d8ca7671de91ec9e2bd5f790d1cb1748e34b3560c9b10726ea4b85b127731d8a7fdfd0ddbed11aaf181799f71a68e542b43ed9889237d2fffe370f41064b810c2e14d1ab661517" );
            iv_len = unhexify( iv_str, "6c58eb8f1f561b180f07ede0d3ae3358" );
            add_len = unhexify( add_str, "00cb63fa0cf526c6db37e33cf092f3f421fd258d28446c9a7c687b941c7eb5e1c5be267db992d0d93ede0b09030f979d451ecbdbbbb386cf1d74b23d55b74f5f4d520c000c9a41922f54567ca7dfcd84c68883a23c7acc3db3cd8d340217ee7c5ea39b41cf2c0e58c270a19ee9e146d2dbfdaf8ba3e24fda7f2c5e4ba6563ef4" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "f0f119bddf5ddf147fe06da9d4510d97369d8e345519df2188b8d2dbaf8b7d3e01f3c26475141aae224e5ce1b131c8096f0e2a17c4c2df62f76f009cfc8aa20ddcd75a6a4281cfa2225485ca22aabcb60ff11265acb92a19ed66797fc2b418ae4b8c70fbecf0fd63f6c22ad62bfd6f40d8d0e2abeb620b7b4f5d8b3e041a53e6" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "7885ca22c4afd7dc6cb440ea35" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024104_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "2e739a485b6293b43535379e3b309fe8" );
            pt_len = unhexify( src_str, "699b9a5668042c48c63ffb323c0fab18446546417b2f33a69addce6178f9d5b7dfa891ff2004eb57a98ca012c2668e0614276d89b21b7bfa436b2aa1582daaa81a6a7722186e99dd16a5786fd0e8b09b194746232fd413984484524793a379112e297d733dce063408fe59367f5929c5086bc2191a8fdd60a346052c0d109d57" );
            iv_len = unhexify( iv_str, "c4deca3eeea80352624c93523f35e0ae" );
            add_len = unhexify( add_str, "704aa36a82d02c56f4992469bb7e8a3f7dda1326068bf6017e4a0c810352b476aea129c1ba1d4974bc0d0503dcf816b89c0dc8e6d066774ce97cea65b5fb5c7b5a7f93e5e2c7126dd3b241b958e47d8150b422bb91c4afc47d53cfc2d20176c2ea0c85b376dc46a86bbaa53c584aa561f6662d11de4e39e50f1a095b8555137b" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "30b8fa2e52577a7e5cdc12a7c619615b134ad4b41893ba9120651cd35c6f2d48ec6b8b9fa99366c4d60e643a8ccb2cbb3568f7647f4ad1a12d14deb8aac00dc4ef780133ee8df8f494675deb7f678fed54e70d6bf43476854eb0286a49cd322cc18daa238d4580ee665fbc759295a3e12567beff3e823811093cf0f02d00820b" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "ff89ee52fa4eaeb748c8676490" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102496_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "6bbb12361c95953a8d757bcbb92568eb" );
            pt_len = unhexify( src_str, "c3fccc5693abe53a13e5209f80611fad1e81e7ce19a4612666d954b4b6d2062bee764181716d5fe0fe1de485bb739d6e8625d5b6cedcaaf6e4e5ec350bc2168c24d7764e75b0cf079d7ad1b5fc24dbed14c5ae4714734f424b3611de0f70a0a8d752fb143e1b7e51ebc965a06021de3718af30b067dde270d804fb5b87ffb29f" );
            iv_len = unhexify( iv_str, "48ca821e5e43fd58668380491d58cdfb" );
            add_len = unhexify( add_str, "e97280fd78eb8bd695227fc79420971081de8f24bc95d9a1794ed2bebf5b68d8b43ae8288eb5ce72db0740334ff9bc9b4e660418d3cff8c344e50c7962c367c26247806d0b5c2ae0420a724203dcf4fdefd6513f8263d995afa4780a9c4e92c25496106fec370d0450d907225190ecccfae634f11f8f74f6422a652b2b9af9e5" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "61cfc5a6ab6847bf0127b35ce0712cbfa9cd28dfb3f0b4cac2624c52cf55f311e55e9abff2d4514c6feff801ea8739f874ded2efce4a440f2acd95eba6c75e09bcd91b898c98563a26b3df415658c4d04a6aaf547a90b03d1789bdf7ab8f09f6d9f222f567461380372a976240b7b180c3fa7b4507e53815af3f6b4a46973806" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "f86d5374d1ad269cc3f36756" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102496_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "1a0a9b2dd1ae31b3e47b6df979dd2fbf" );
            pt_len = unhexify( src_str, "353786f96620ae7dfa7aee163c7bb30384bb324b516cad13872f48e7251f6f4c5906748bf2a2f6167bc14453b2b2f513804308ba92d69639beac2f25274bd5477744281b7ef7d0661b3672cd45abd5bd30d98deac4ad0a565308c0224dff59e3190c86df6a5c52055f8e0f73fa024f99162219837c999a9c0a12c806f01227af" );
            iv_len = unhexify( iv_str, "b39c8615fa062412fd9b6ac3a7e626f6" );
            add_len = unhexify( add_str, "dea75b17cd13dd33b5016de549c44fa9c88baf424ac80c4835e868acb58082ffc4255c655878a1c627a44160d5e5054a0a04f65fdfb542cd342be2aa2e000117bf8cd67b02f3a3700755508f9af8379c226aded404117a5ca3fa70968495eab287064ee584b4ce596612f2c465d997518c6995518e3bb881967ab6b99d7f62d7" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "8430b8735f0b002e098d513eec7b3a8431a3fdac2b7faf256a7bcf08f3dcd6fa549f029240acae4dbd4ad54752ba358c14893aaa67a003261c252020d14b521906b23c37dd80af703c2964ce13773dd72fa56c389768c6efbd485953900b56f6bbaa837f1668f478677621a297d4b5a2c1a86f689d8644caec51435b0dd66c77" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "f000f2d398df18534428f382" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102496_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "4da736fba2b7202ea2ba60793da3344d" );
            pt_len = unhexify( src_str, "4f004852edd5dcde13507252ed8c2b20a093ac9081ce2a8133c48d2807e5f968c04a20dd52c070d6c43c704b8650da7f94e5450e0d34cfc2b2d2ba7cb5343e6b4281633c6c065dae27fab18ca71bea018eba94d20e78c5e3223c70f50cb77399c1a89436f1e7213673ae825d4fc5523645031696df10f9b5238c03f733b4dfcf" );
            iv_len = unhexify( iv_str, "8572af442c9af9652a192d893c18b8c3" );
            add_len = unhexify( add_str, "429915c3309fba2a42b8e89f42a9376a2f329805a4d6daae11e9a20c2f982671ef8a7539a9657777d03cbf755ef93be0d8e426ed00899a59e8b963fd44269d64692ed07b231cde93e85397cf125a75032ca3726ea1ff1b05d79f2040c1135012b90597186c1db2e16cd128d45a7b9d934ec01341d9030e9721c62f62003059b8" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "ff4e46c4236304b8d52ba2d6db269f95d2cd5fe4318ce930d407051469c7e36e44bbcc909c4966276f5a2ec70021982fecbeae34df235a3e9e0370afa5a269ca8847a84b8477f7ddd6055d0f800ff4d413f63db517c96d15dbe78655748edd820f2ee79df5eca31711870022f1f5394b84f05bfef97f99cbd6205f8e522b3d5e" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "624b0b5b6374c5153835b8e5" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102464_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "5bcc874114b9d78c3eb748a783d1448c" );
            pt_len = unhexify( src_str, "7d57418bcea007247f5e18c17a2e4601c3eb8c89f61ed365d5aebee7593cdd63871d964a25fc9d723f291d39e0c4f75012471faf8e06db60c4ad8a26cf434bd82a29a8b653fdda1b86a7e4800c1d70cb5d8b8a1d1af52894082bb282ffdde8f0128a4abb68aedcfcb59160f6b5aaf452812f4d00472d2862a8b22480e71231b3" );
            iv_len = unhexify( iv_str, "5f4fde440faa9537d62e62994ab20fb5" );
            add_len = unhexify( add_str, "b5dfe0d971f2920ba4c029d4c346a49788b499faacdb18b8f905f1457a8b9fa48709893516a7b48bc601710bfd73c12da094c29df5776d491c9978f8ab237f605785b0304488f1c20bf5a767ba6d5e1e2961957aa107bdba2358b81ef1e06576db985b3ef8194725b75d49de1de3a57f161dede508e37ad3356134fa0a1aa48e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "6bc0dec98bece6c4e245fe978f6db113deca75e1b475bc31f1da0c7457a85ee7aac8be5f2121c0610b99a2c64519fc2514b643c379b4f53c5432b9729aea9fcecb88a2e2d0a6e74be04859a66f55fb2af1598bcb039108ef7fcfd99d94e79287ec1f62bd1bf5ff9dd51ab12fae4f6e21b95ca50032f9a65bd85f9a1aa0524950" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "354fb8bcd38f2a26" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102464_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "427c89146eb7d76578dc173bd9e15cda" );
            pt_len = unhexify( src_str, "1d39249130404d60ed40241cf3354458e06f1474b3723569d88235f03098053fc99010f39435620acc710a4e386b2ecbf9b327a8dcfbeddc084353fff029d24787ce81e74a5e1ac1ef096e0a2ae882a669ca168275806bb7f462e66c941fffc6ed44b9628450e03a5032676c1ee4aedfcb1767150d56c7d73a8a47f6d19854fa" );
            iv_len = unhexify( iv_str, "0092e76cd8882e5f77f4c8514491705d" );
            add_len = unhexify( add_str, "0ac4631358bb9375e07756692bde59d27012e921f054fdfea0ddb242c43421f4c7241cb210cb5c172d053de2763efd565f1138fbe7f9cd998d825ab800df900843474ebf857b3371c555b89670e86354fe430f715ebbd0ecad974fea34e3bbae43d3ca3ca178f3361f0a11fd75f60e9140f44364b02a073dcce8339fa28cb5ad" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "2b385e9df4ed41cdca53a4ac8cb3e0af75eddd518b6727380712950d96c34bc6a0a6ac02184c1987548932b116ec9ae7abf01157a50e422b3e6aa62deb0cb2d81bf7fe0c25041a355ccaaeb049abb0393acfe90d869e9edfdfb646971bbb1ba9e5983cd0e2739158fab31be26cfdf9286d347b58b00f75d9f48ece1353308a91" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "905cdf228a68bebb" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102464_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "2e09660909a9aa0a50958016c3e07895" );
            pt_len = unhexify( src_str, "d7b2ceb182d4a8ed57572c4237ba99bbdd589093db0f71732f9e67559d3054fa1af195aa4864fde413549d27468ffe7c5c23e242cab4ae4bb9e2657422dc3fc78fbdcde892ed202be1e47f095b09cfc53cfe86cb16e2e95444492ad5d0eef053178d6b0485731be7a5193563bf56f63cc0687fc01679254d74e9ed788645004c" );
            iv_len = unhexify( iv_str, "c4f865be8b5062e488b1725749a87945" );
            add_len = unhexify( add_str, "26f50acdefde4d585fc6de6c6234c9ead40684349a2bfd022df93d9774c9f5b8f50474032a417bdcc21a74da72c0297437a0cef8f527c9205797f77b4227c272e08ad0b120a2a31ef13e372cad2387ccc1bcefc88dd58899821d68f3be6a4b2cd08697d1897efcd6ed3a0d7849f6cbb50e46800627cfd26964e2cfe9f36624d9" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "321f6d79a6658c7c2b67fe3c932237593a6ec7e6fd8198abc6b0b6ba5d4dac9e0695f0c64dde1c94c0383839ee37f8bbfcc516f24871fd79a9b9135ceef841e4c8ddf6b57962c0e8ad7aaf210e97a43489097270756404fddde637de461b8644fef244142820e1af12b90f16748b0915a6b773dfbbdf6b16f1beaccb4cd5edba" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "b294db7ed69912dc" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102432_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "5e45d57981f65a6b170efa758cf4553d" );
            pt_len = unhexify( src_str, "bc8d4c418442743f2fdbaf95b8f87b7c15a3176085e34addf4cf0fb3c2df15587526691b07e6407ba16999b72382635a2aebb62d05c1547a7d074c857a23107c7577864e7f7bcdb5b6d1fb50136391f89c42d3f02754b0e4ed0fcb0c03576b986af5c12cf9bf5e0c585d6aaf49d0c6fb2ec30eae97b2b850a35474bfb9a2c069" );
            iv_len = unhexify( iv_str, "b43403b627fe9e0135192d1a048c6faa" );
            add_len = unhexify( add_str, "7a27ea26c7607e4e7e627f3161bdf15f21f3d62dc33df14951971712f960d3b2082d75395c5008e5ea00d282d350f86dac8c61f5c0f90e7797a5b61ee96f7e332ec5de51cb1377e47c641f326d1e58817c8c95feb5b2923758e33b279191d0a9ffd09b7619b0318a70775e36abf5f7ab59422ff68914e7b478c448a7b141c4bf" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "90d8a6218da063c38e0f06d548a3d5685fd3e0fbaf609c77bdd573bb9c63f30590eaf8b181a2feb81c8b3f5f34a94dc94b905036a6c69b97263302b8674d9e09325065588e97c0b5b33116981f1f362a7c5bb1e996c126c31fbd63791772f4d594632f408fdf011b3f2cc750b060452c181e8e09697c8662c00c8d4f29d875a7" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "611abef7" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102432_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "00d4bf20509a61bc76430ffa5f013589" );
            pt_len = unhexify( src_str, "036a191a388cf3c57c9e6f0e2f5c8bc3d5c25ee8e2fedfadb7b7433155c7e79304f0905ab2a17e1f04f2f2dacd4a41521d6ce213961df9dc9101d41df4e44246488fbedb75a01256fbc7784769eb8f99d44d5eabf93cf667ebae2437ccedc79efa58c075183d46a5c20bf4c81e0f9754ad35af65f7c8aafe7daa3460c6892b1a" );
            iv_len = unhexify( iv_str, "25b1026a009470a5ca8caeeb67200792" );
            add_len = unhexify( add_str, "fd75acfd5aa25fb8bccb53672e5d6a8080081506cf03df2bab0746a353510996e0237d6354ee0210a41f20f88ec6569f2b200b28c6a31464a0533a6bc45afef3ae381425a3606de2866dba694124d96da9d0a2b061b787524ee6e5d3b1ef5c4bcf168810aa177660b7e1379ac8a480ce43d73dfcc696873cea2df419f372651e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "cab80615b666c47fcabf0d9805842ab2805150abad4de0ae8b12306bed504d4a7f91f52379df65cb9587577e59dafcd4203d2ed2743d35472285e9522db0ce3dd027a01c79ac64caee29ef3752a077254b0dca269f6f206f6cc575e8fedb0ba525dcf6252fa6f7b688556933f1dee84b2ad36a266695ce8672229cedd82f20a1" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "3287478c" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102432_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            unsigned char tag_output[16];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
            memset(tag_output, 0x00, 16);
        
            key_len = unhexify( key_str, "fe481476fce76efcfc78ed144b0756f1" );
            pt_len = unhexify( src_str, "246e1f2babab8da98b17cc928bd49504d7d87ea2cc174f9ffb7dbafe5969ff824a0bcb52f35441d22f3edcd10fab0ec04c0bde5abd3624ca25cbb4541b5d62a3deb52c00b75d68aaf0504d51f95b8dcbebdd8433f4966c584ac7f8c19407ca927a79fa4ead2688c4a7baafb4c31ef83c05e8848ec2b4f657aab84c109c91c277" );
            iv_len = unhexify( iv_str, "1a2c18c6bf13b3b2785610c71ccd98ca" );
            add_len = unhexify( add_str, "b0ab3cb5256575774b8242b89badfbe0dfdfd04f5dd75a8e5f218b28d3f6bc085a013defa5f5b15dfb46132db58ed7a9ddb812d28ee2f962796ad988561a381c02d1cf37dca5fd33e081d61cc7b3ab0b477947524a4ca4cb48c36f48b302c440be6f5777518a60585a8a16cea510dbfc5580b0daac49a2b1242ff55e91a8eae8" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "5587620bbb77f70afdf3cdb7ae390edd0473286d86d3f862ad70902d90ff1d315947c959f016257a8fe1f52cc22a54f21de8cb60b74808ac7b22ea7a15945371e18b77c9571aad631aa080c60c1e472019fa85625fc80ed32a51d05e397a8987c8fece197a566689d24d05361b6f3a75616c89db6123bf5902960b21a18bc03a" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "bd4265a8" ) == 0 );
            }
        }
        FCT_TEST_END();

    }
    FCT_SUITE_END();

#endif /* POLARSSL_GCM_C */

}
FCT_END();

