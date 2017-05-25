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

        FCT_TEST_BGN(gcm_nist_validation_aes_19212800128_0)
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
        
            key_len = unhexify( key_str, "f8022b8988383d5cfd7d9e0e208146e7868d3d714fe85744" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "5fccd8cb551cfc9c20998da4cb981d49" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "1b5c6c9a28f5edfa4cf99176b0f14077" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212800128_1)
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
        
            key_len = unhexify( key_str, "a7d4456b8e16b82283b677bd8c4b1f56dc7f153b5cfa746f" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "081de4a3f71f5d6fdf7801ff6c667f7d" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "90c2729c5ba04f8f5c73726c910640aa" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212800128_2)
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
        
            key_len = unhexify( key_str, "5779b60b536b096c9348cd8dafb3451280791e319b7198c2" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "62f8e195bc79957ca8ce99a88ded1a02" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "699d71bb63c668b533c357662f861513" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212800120_0)
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
        
            key_len = unhexify( key_str, "966cfb078f695c8ad84ede2fb96fb89488fa271dd3b50346" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "4a7b709d45745d94c5433b01fc9d57fb" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "4a9bd213420629a5f6e471650060e0" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212800120_1)
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
        
            key_len = unhexify( key_str, "cc69ed684af2c2bd2b3e2d2f9faf98acf8071a686c31e8e3" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "0bd4197e5ab294ab7ab1e6ec75db2ac0" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "6632b618b4cab963dd671fd53d2075" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212800120_2)
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
        
            key_len = unhexify( key_str, "99deafc5ec6155043b53a86d466c2b652d59b7274bb844ef" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "09d18e85e5ed38f51e04a724faf33a0e" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "90bfade2f07f38b2192e24689b61cb" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212800112_0)
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
        
            key_len = unhexify( key_str, "5c0c706a1fd48005e0fd0ed91b4d9f0028c500dccb28ca73" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "595716e15498454577d3581e94f5c77e" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "8b10eacb1f127f4c58cbb8c3516c" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212800112_1)
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
        
            key_len = unhexify( key_str, "ae8e125507ea16d5282fe8bac42d3cb4908b717f345e6a38" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "0a7f64edb8cd8052fcd5b92e20c0bc2d" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "467a2c0ba1d24c414f758200b8a4" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212800112_2)
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
        
            key_len = unhexify( key_str, "02176a5a5d8cb8f5ccee3f66a22181765ce730751c135198" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c19ed1f52f5ebbcf89ab1907b9ebc7f7" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "6525beb5856d6f29105777e31457" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212800104_0)
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
        
            key_len = unhexify( key_str, "4434d6bce3a33551733d7afe8cd477a79be8eeac19bc0a05" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "b0eafdf326886eaacb750dcf2c104abe" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "ab9f7923a3b9228cb9ecd7f907" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212800104_1)
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
        
            key_len = unhexify( key_str, "39994c2520a6196cc3f3e8c6e4833286ce37399e0379563b" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "dbf9c40266d95191d70739e932cd8572" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "b29acaf5addd6b379315535375" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212800104_2)
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
        
            key_len = unhexify( key_str, "1f27d054114a264b37ee1821a077773750cc79d28594f506" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "6739d43092620f44b57e65035ce14565" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "25e0434a3660704eee4bb82962" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280096_0)
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
        
            key_len = unhexify( key_str, "0e97d15f4992a6354e43944fd346da65ac1f0f1229189442" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "32a64e826b500d7e85f4c42a784f7c19" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "da8f3e0a6f156ec260aa34fd" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280096_1)
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
        
            key_len = unhexify( key_str, "27504fc47a9e9a85eaded3782cb5b088359ea1c0abbf2730" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c55c8dc3d6d2970c81659f2f87bf849d" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "113e637538de291e2463abcf" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280096_2)
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
        
            key_len = unhexify( key_str, "d5fc67f73de736768e5c64c37459c5eec3d27f7e337c346c" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "2691432d3935d4ea8cb8f7c17bef3558" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "c0af76d6f62430106ca54928" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280064_0)
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
        
            key_len = unhexify( key_str, "f28292ee2c54119511a67db0d2317433abaeccabfdd5d1f1" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "cf9331a1bb3851b2fc3aeed2d1a33eb8" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "8e14b869a95eb12e" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280064_1)
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
        
            key_len = unhexify( key_str, "2042f9244079736291ba7fe1f030cba99672a97ce361dc14" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "aadfa619bafb21b5c738b65d632bb8b2" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "ad6f52f25aea1c55" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280064_2)
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
        
            key_len = unhexify( key_str, "d9b4eb00ac03fabb5304ac38414f7782cb0186436a4b9036" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "809939260117b759d8dac1a69c27c12a" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "1f7d0b3104aae50b" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280032_0)
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
        
            key_len = unhexify( key_str, "b5128f4cf91d53b3a50e9b76b0b27da33cbd4b9349d89413" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "644909f5fbcd61d850e43fbef1fb454f" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "2ddbf709" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280032_1)
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
        
            key_len = unhexify( key_str, "3ac7ab2ade7a8e397d66be6dc7671f19cd39ad65490f1712" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "d152359d765f41dd9cabf5c8f37cfd8a" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "a6e4e30d" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280032_2)
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
        
            key_len = unhexify( key_str, "f9c2de7e3c74b7e318413a32892d4fd070de9882158bbc82" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "63410c83fa363a63fa78303b9994b6c6" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "49c514ac" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212801024128_0)
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
        
            key_len = unhexify( key_str, "66ebdc2332276784a69b6bb137161210bac9f1d6a36d6a4c" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "647f41b60c6a579086ba8854d043495c" );
            add_len = unhexify( add_str, "da26eebd04c27bbe7fa7b54b87d3b7227f056dd9c085fabfcb59ec665a257c6de68fd2c1c51aad5e6188e02a56f70aac49ba489802247ca327de57ea3cfa87e72cae7dd82b50341a2133b03cd0027216fcd94cf43ec8a48e1c04145b597924b37f7977db3ff23b8edc913357037d0fe02afe2bba6b91e27554edbfb77f51cc41" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "420b320c2d616a0b11a7605a84f88e26" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212801024128_1)
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
        
            key_len = unhexify( key_str, "26b04d8427582b04318fefebac2a2298ec3ce61146f39a35" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "99f3449c8538414e7ab595b92a7e6e10" );
            add_len = unhexify( add_str, "edfc2aa8ed91cfc0e117fc9e2d1bfe843c7cf365a2b6cabd4259686cd7aede9c7453623967a30ffbd52b30fc205208bb346ffc70584478f5f39a79d4971ed71cc3dd0200a89aef6aecda0a1f3a4bf2929b7b9e141be0ddd3671f727e5e793ef085f52ecb77a266b9a02a2c700b63d8c43da0b569510285e98b530abcdbf7739d" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "091cfc38b248460eafb181ab58634a39" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212801024128_2)
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
        
            key_len = unhexify( key_str, "82c8197e6641d0832639e2b1d7691fbac79618b2f5db45bf" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "69e1a3e5eed54bedc941646e3ad25a6c" );
            add_len = unhexify( add_str, "d0fcb4f4d764efc0fb52c8108e61b67a1386f1a13c1761941cc9a28c6ad15e78474cd2a65ae9475d70d9c845f14bf4d2bd2bc46c29e507a347391829e0f24495b026f681c387b3e6aec8acfa5ecaf4c3cfe796c22469478ee6744cf04a22e6aec82489f53109551f58cc6602933d1780b8b45b933f76a94ef652a8ce8bac2cc6" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "8e74343ae8cf1cdda4969c1a94aab5cc" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212801024120_0)
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
        
            key_len = unhexify( key_str, "1a349ba960b2c8f49b7e5314911ba8de358f2e74ceddf126" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "f5998a62ec507c5fe5b280f9c57ac626" );
            add_len = unhexify( add_str, "78445eceecf2e6d2ecf2589fd24e854bed3aecc63aef934aec9aea93dca95d58629002a4ba91e9bf6d12e13f0a844977b3c2700645281db5de381adbccd34a84346a99f34889bd46c75b1956e21aa9f87684af55d7fd0de6da07e856d9b791c0a45e9e37881092f6040a9ae9d87757142d3c9c7fc6f25db0e5b5d377865ec4da" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "4d7eab0a3719fa53e552b9e5a85bdd" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212801024120_1)
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
        
            key_len = unhexify( key_str, "019af03d23342f7916e329b6843161e566aa859402cb07ff" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c5fd96765fcf6d51e23ac6d206744af0" );
            add_len = unhexify( add_str, "f9808af3403051a52b6652df03b6b37d90a471bc242c436cab6ba699139eaad16847665093798731b9969709287199233c5e77351c5e42b15453b4171237a6d16aee63773c8c0d736b3a8bf38ccf922e561c456682fbc2c7161da3b89526d9de222351bbd04ecd4e8680f26d70fe57d577ea287b199be1bbb8b76328ddee3d33" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "fd36fafe4f5571fafb6ece59b77381" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212801024120_2)
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
        
            key_len = unhexify( key_str, "fab39ad2946b2a343d76b1ccc1939cce7ae3cd7b6ea187bc" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "247bc71446489dd3495c4dee8a071c76" );
            add_len = unhexify( add_str, "cb2c06fa5aa54ad079741afc56dbed79061a02045b6c099d0ae2d7883b78c5fe09636cc8a5dbba0c0c76ebfdb81217526afbbe04fa4b2b78f3357025930b0f9488369bf3aa088a2107bfb6c4ba714f1c26d0380d647ada5852d2c539300a4779295412b202c3cb977a7b94c24c4dd2a891a2035f388257b84e5b31bdc895f062" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "65e1aad214f49881a067d8b372ab6d" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212801024112_0)
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
        
            key_len = unhexify( key_str, "57b52697f72ae2df6354410a69dc3c5f28b31e6617bd78c1" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "0d96720526491d196eca66457e3c9e71" );
            add_len = unhexify( add_str, "cbdfdb3cc73aed4297ff9aba76dd8ca4d8efe11b0f521fd7170f07461c7885252874b2ff8fd05a3943ecdc824ffcef0396980ebbddc0a53c6c99086c14fc806d90d35347d45e556e9a55ecc3a9fd74c8e5dbd19ed8b452eaeb673818ddc0695f56ddf3b139a3df378fcfe5b6ccfa358f5a5bcd1550f1d9d5f325f15f9dcd007f" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "f0c49960e60fb63edbb50bfebd98" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212801024112_1)
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
        
            key_len = unhexify( key_str, "7bf69ed06271107e11fdf016edc4aafb0e2d2ac05bdbc46f" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "50e65aa338cfe856c80cbe1331b46abd" );
            add_len = unhexify( add_str, "a7cab4e1e56f4b9fccca08d3791560e4b6c7ceb40a10adec0536861c5c46fc3fd06c0a8eb32c9f18c40463b0f06cd0053e615dfd7caeb2b353b08ad6da1f8a23ebddf16524d2eaed70d4d7e565412dcc9598df7e107beb464b103cd8de9301cafe8b0420f0c156025d72b73d6e015ed2312535d35899aed73aa54374674d7f02" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "d7fb9d78fede77981948eb013ea1" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212801024112_2)
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
        
            key_len = unhexify( key_str, "caa781bbed41d7a1c56d47673f74d4310a3bf8b1275031d6" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "7795dc04261d9433367f51c3b87bf18d" );
            add_len = unhexify( add_str, "f44d77bd541e02a737c693ff3ea0adc091fff1966a593524e68954a2d7d66a48199366a5a600331cf392965b5ebedbf949203975fa9db53b72586615975e8a7b84e0633c6cf69caf482dd72b26b0a5687ec71667e7f6e5abea89c3d69d2dc42a242ef959e4039ba5b2d22a3e48424a431a77e816604769d13b7f892e2b33fcd2" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "386930ced9a46097c0d1f6e65c62" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212801024104_0)
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
        
            key_len = unhexify( key_str, "1b268de4ff644cfa4361f8014656d5d4decbcf9cede8605c" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "4009bb00afad026cbad117c6047f4ed8" );
            add_len = unhexify( add_str, "140c5a93293598fab85b3948b53e0ba15438a0b948e91041a13104f0ad263c8a10613e20e87ef261999a54d469ba6f1abe56ec3979623df8520a0476801987c15410ec24f5a9be72acfca71e8c5904e2ea5f8b22b8cf404b9fd533aa37e33b3d4cf91599cbb3b85ecda4aebaa27ac0365df8312c399ba1767c47fe0923f2c53e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "af36bcee7561cd7d0861085d55" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212801024104_1)
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
        
            key_len = unhexify( key_str, "c2843bd689ccbba60ce961b7dd50619a59234dad97567e39" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "55a68cbaa5755d8c67bf26f03c5863c6" );
            add_len = unhexify( add_str, "d7980ab86ceb9b66ab265b68e078deddf7ba084b8967c3227839e8f31cdcfbbffa004953f3582ea9274dcf46e3ad7e7744a576dec37e0cb36fced2b2c2fcf4328f506302f5741e696ce25c49492e33c6a0c8aed5af03cdc1a266352623c6a52a555ce906f684bfd597b5e37f60b5175a981088b9d8b8b5493e4fc1bfeca64f95" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "66cccb7d28d3fa70bce2900a84" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212801024104_2)
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
        
            key_len = unhexify( key_str, "f451c5edf9849a390486dfecad437cb809c33d31f6898ba0" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "9e2dd52c04651ceea88caf4adfb2e8ee" );
            add_len = unhexify( add_str, "87b804d4a81dc203d67a92b4fdeab959c2056dcedb28d29f216f9172817bcfb3d2256bc1c8aac23feb22b71f1fd02ea28cdf91785931750ba4865d672345b5001b1aade4f6acc7edb03758d2540e6472aff50ab3ea61a0b9ff37ff7a87b91013b14867c3e43cb097a923e6d8ddb1f52e4bd940b60d500a4e35bfa91935065f26" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "e192a49f5f2b22fa39dcfa54c8" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280102496_0)
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
        
            key_len = unhexify( key_str, "bd02ff8cb540ba572af3431597bdf3f23e61665f96a19b4f" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "7198af3f594a4f0597f45fb592edef50" );
            add_len = unhexify( add_str, "ef06de48bd34f362fdb425c6e35e37d0dfa1ea874df7d201b6a1c25b736c96e3cc8ed0915807fb7ed759482ca701d28c08cbf955be244bf887df37394d1ca4d2e7eace0dc61c807b714f3161f9d7f554c9f87ad674849c136108cfd8f777997656489d3e993aad4a51b68616083876832b3085a5f8f154b83ea44702c70f2980" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "43298281cd27a36e5cbac4b9" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280102496_1)
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
        
            key_len = unhexify( key_str, "9ecab4a4a9dda43477c993d6388387443c66ac253071c504" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "9523b2722b927dc3afcc5f7dab2bf033" );
            add_len = unhexify( add_str, "fb84e38a84225c8ebb307df88325d020a5853bb05ac7a75ee38552c40c302d263181081b05918775cf9cd6905b9982b2ae9ef7993f28fd8714e878c9a4a8101c08e9f13581dcf4f16dabfcb9d3c471c0056805f51e67e9b75572639c3d6ce62d2f8abd64e1e66ffb292360c20155e4d528374a5a22d845340d6f1ac68d33040e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "696bb674e43cdc7d69346555" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280102496_2)
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
        
            key_len = unhexify( key_str, "733df8c42cc2e70ac195615d4911ffbecbe2712230c5c292" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "f76135eab5d42e82aedff3090a1ba606" );
            add_len = unhexify( add_str, "0c8aea747cacf2f0fdfaf368cf32b12dc49f5da9a29bee380d2d64035b73efb56fef13aa20c0b612d9615cefb94f26978fa0b371a47dd20051a1605b9f5e133b52dc514577c53319c9e2bd4ac7cdf37d56a9e715e27860a09d86cc21d0b9f0f302f6acf06f2ff00cc6c878dacb8bde51082f701314de7efd36a246f80f8a8fb6" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "82e6d0c076c7d8ac0839fe18" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280102464_0)
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
        
            key_len = unhexify( key_str, "ba33c24c41bf9836607b6dd05e66a3d16298c897dd1d70ae" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "4b30423df6de76dd684274afbea089d8" );
            add_len = unhexify( add_str, "71f5f6ee7bbd774fa691a3d7e0f694a6c8dfe8aaf9cd720e163ef6d5cd949c798f9e9c993adb6d64e7220aa0f17331bfa9a43b659be101726a80e5529e827c3e4b05cfb4d78db9952e58eebe64dfbc0d1baf20e7e48902215277a49ee953108526a70ee150eda85e6a0e49955f8c6323766ae10e13ecfdbe4815f4bb4ba43786" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "73e80018235ded70" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280102464_1)
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
        
            key_len = unhexify( key_str, "1711553980e3fc5c14c98611ddbdf426463f82c66df83a70" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "3396bd96b83ba611ed22e12e8a5ec911" );
            add_len = unhexify( add_str, "9506f34c90611acd6ecea385a782a5739f88b4fd13b77570c4d7e0617283e7b21568e32c42ada1cf6aca1a2e2ba184d4101306ff21c9d03e0ffda4854773c26a88a5173d52960286c18753df17361bb7046d2884ee600f58775304f49cf4e782ac70cb00b3d9c345cfcb38e3880743034640bbcae83112543cd1622ebaedb221" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "5d51a0868a2161a5" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280102464_2)
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
        
            key_len = unhexify( key_str, "5d69dbec7ebe80f2b5b8f61fdff1f4413f5f6624010fb795" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "a2eb3ba50dd93fa375cf682db7b2bc7b" );
            add_len = unhexify( add_str, "a0f9c0de86b54d3c176ece3305463237e1f70be3c52e2ab1c773a9d27d6fc5dadf61ce7a3d10dba8730d12c306fca8952403983bf242fc1b6efaaa153ca446a07d16a70af4cb1aa4d4c0c93d646dc3a5630f5a610aa9e6eeb873f9a06d3234642bc86b03c596235ec03019e762458abe17d37409a18ca5b7e0e0088391dd3acb" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "1a827855ee98d679" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280102432_0)
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
        
            key_len = unhexify( key_str, "7aa732879f290aa442217212156920c69457b8ec41eab153" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "cb593221c59846dc82fc0d0cd04af3f0" );
            add_len = unhexify( add_str, "15d7ebf94985c34b72b6675d7346f0b05bdb8fd3a278555939d2999028e4179e69352d398a5dd0e5b370bdd9cbd24d576b89709c98b6142f71f5b1ba224222afb67599fc58fe043d1a91d7ea95b56dbd086db8e3a061b1bfc6e82dc9ac728174fd3669d65db62a06380a5f72c3d091b7a1b6998041d5501e9fba8bf91a7d278c" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "55b86d22" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280102432_1)
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
        
            key_len = unhexify( key_str, "961a3e78f6a75944455f9d9d0345e08f4669972f3d5c202c" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "ce43a19ac648e62ddc49d243fb34e29f" );
            add_len = unhexify( add_str, "393736558133078a0367b8248bc18c8352f92a9212e90318a5b63ad3c422ccda7c181c565629acf4fc73b2de85bc9cf38310fe703a877b3e7d3b2d416aeb962f1027077232cfa39c5e5284a1b323264175546ddfb250ce693e2dc78a0479bd89a7ab44b63e504866d2ec6b5153cfd51f29a91cd4fa2b8e09878747ae53981875" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "ac701373" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280102432_2)
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
        
            key_len = unhexify( key_str, "c4d492904becde4e46c2557ac833265c715bb57f18cd040d" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "df41b22b92d43a96a7504514b92e644f" );
            add_len = unhexify( add_str, "c4dd46ce3e486d89903482de247c1e7df05809a247302db3ca8457b93d6886c0a3d1be40a90f6502ec58d0ddd715896cee37322d48ec3f0c3ad716f1bb679afdcc0e4c79e5e2e346702d349ec7b391ef7eafde618bbadce5d14d22123de611c065780a4d05e928e87d12b749888d6004224c3e457aca0190bf1a7fba2453680b" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "7a259bda" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810240128_0)
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
        
            key_len = unhexify( key_str, "316660f013ced78a16701b35479ffb1f7c8c4e964c1b52b8" );
            pt_len = unhexify( src_str, "d262c15d08aea46f614c7f8f6a54631289e54ca97d698777388e137f431bb783601e7999e7af98775d7b87ce061d9ba56570ed8c58b6bbac5f12f751fc376ab0f14b36b40b2b5533727be3bbc9a51183405d5fd0121201ff592817d06613b504a3440b0e1a57e9ed1771766a9a5b789054f7032d20b23c5c37d77f293c677fd8" );
            iv_len = unhexify( iv_str, "919ceb172d2cb460bdb3b3e58debe889" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "5f5128f7f948f0cc9fb248a24b07c54247e40080a992acddb2615d90ef9328a17bd5e9a698b00103855738aea55c4944cde4a9148bfa8db12233231861c455e52c9889119ca402eabc8f41b27000156dd29b901024336cb2b7088eb5fd534ba58f23caf140a8b2549486074e4edbfc262ed9c7c7ccaae24be8de873ad43cd13e" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "ae22ec4c19e7616a5b877f168febd202" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810240128_1)
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
        
            key_len = unhexify( key_str, "1bdb707c328304809bf0608874c9db373df3c7104a5a7049" );
            pt_len = unhexify( src_str, "ca243caa145124997f5e2e6bb25d021a38d58d0ab1bbf06d086c2416c08e3512aa887cc215fdb34d0f2d78f6a45885767f15fc00b68a4df1130587de777efb9cfd59cafa077477e97edabf2bf04c9a6ce029c230385ca5f9928bca7fe5503b18774849199d2a39a378a2d3144aef4416c1718319ff1bed8021dd77a07f61eaa6" );
            iv_len = unhexify( iv_str, "b7e7fc0d5adaed1632c5f7d1f56458f1" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "91c7954bdd6a49360fdce11c1bc710512bf5a57bcef241fb63e5ceabcdc9699d0c0ddb025c75195ec25e631507f13e18799e6be9798e5639ad8401f6244c5b0ace3905ae0de08e2d0fcd19d193de83943fe449af4b503a454c248e677d2f51100fd9b8b7e5388d5091089369a7c2ff38bd353e9757ef873a87f15f30232bafb4" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "72337bdb2bfdd1f1ebe0dba6f9b7b649" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810240128_2)
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
        
            key_len = unhexify( key_str, "a6dd0d7e9d6ad1ad7c7394d53e9e081c436d34c8158bbc95" );
            pt_len = unhexify( src_str, "2d95d64ed3be857a5c79c7af20aee00f142557e10d780383fef2d45f16c7e2823ffee495b220c87971610e5650f7c3e8d296b3f03fc883c00351df48d97717427101aa0c08a23c408b24511621b640c210b316cf17e3dfd714f0c9aa9ddd974692d1c2ae27b9bb0fbb428e7a9da3b3cf9bd869e730ccaa3aa4bd08f01f84039a" );
            iv_len = unhexify( iv_str, "60b4b9c77d01232c5d3d4af81becb0dc" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "4494460ee73d3513814e1f779bfe3a229b49348d7641e9ed4dd959b582960097ef08b91292bb9db87b4e728d01b92683f4cdc81151a69bed2096bf6fb2e45d0148404420ea16b631b421e6f4c6665fe33c2d11e7b22b6aa82b610b83214ae4d17e681972e3a1f77306d3c54d96c47d8be1fb2c8cae8300ac9db99013f25a65a1" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "d40a246c18518ea9f8d733b42181123c" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810240120_0)
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
        
            key_len = unhexify( key_str, "e9ed78cb5c10df05ad00c6f1fb35b4d28e6ddfcc16456807" );
            pt_len = unhexify( src_str, "e465e57cbac0dcd1e8ccda22042abecd9d89c4ac91b0e82a41fd51107a792099e63f7cf23a889d8c04edae2c2b3a9e51dbee6c3b71ace3de08ab354a295888bb99ae0fe428dd69bc013d49a70511ef60524282347787a542fe9501b6452b5faad2f129a9795c2c4cc0881ec4af8f0e0d2d4a7a628cb051055fe831b51e250608" );
            iv_len = unhexify( iv_str, "3a8ad989c621ae1e82b8d255a3c95028" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "6855e4702f1ea593bfe30ee65b3fab832778d6b11a0ad902dd37361b8d85ab76d1f2ccf7927d695eb3129286c26737b9573e26bf64b31de26f97525f84345f73bda2888a1f53c9b405ad627bbe5dea123c9fb0a4b7f193cd8fbc8fa4a5e5f64e9c083f5c048d61fd1d347b49afdc69e0ca6a82e3b064c49d5bffa2800b5cfcdf" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "9661f5c3b0d99d4f762bdcabd48df2" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810240120_1)
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
        
            key_len = unhexify( key_str, "76a5bc9a8d7c6e2822456156cea7d493702d61e7d504e3c3" );
            pt_len = unhexify( src_str, "0a7fbca875fd620c3d8de788e5c27534160f68d60d70fa4167adf0c18ea25fa1f2cc551fdf447aa16678d3f82193cf421a6fa953532a3765bcb54183bf0e96527ae5e695ed3bba5d9911f36c1aa73132cd43b2698996eb43ff84420e315a06d7db02aee815461892c7ab9026953c4bc25f47153d5cb7b966b71b24dad69fa565" );
            iv_len = unhexify( iv_str, "09b681de6683751300c2ada84a214d02" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "dd66e08fc500426feb497c39c5853b26376272dfabb82ab5978167faa91adb025a6ca0e8fe3d04a0d97062eee8ca6530c3788bebe4436ecdd3d9eab96d38a0cf9b8cc6a584a0facaea33ec2f4a6e61f780c3dad524df902f421e3204cec7c9a4bb3f0860e017eddeb939cdfbe6f924e1eebfbbf8ec63c55b62137d9f8845f38f" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "4acc40a4882d7733d8f526365f2560" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810240120_2)
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
        
            key_len = unhexify( key_str, "f5cb564cdd6974219e87f93a030fdcad35313d4adf9d7a97" );
            pt_len = unhexify( src_str, "210a799d480b4933e16fcbed632579beb6b00aec74c205dbaf64e2cb152c12f9b6969122f296efcfe328f54202446514066594848f42a3031425020b56d065d6eaf2caf507d5f51df493c11514400b889f33d0b996e721eb613569396df0528aa14eaed117dbb7c01d9c3ac39507e42a158413dab80aa687772475105eabcbbf" );
            iv_len = unhexify( iv_str, "90f91da5239640a70eec60d849d9ae70" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "69a3dcf5b94a507a53fa5e62cfca269453623ccd3a537d971130a21bee884cf271b9833dec19862ab0dfe7052e7dc07b20f34aac42bc39bf1d495360c1d701ea53a9bba64b02962b4ef64fb1c90a1a2f3a6f81a6ba781d5f28b786efc365ec6a35c826544aab94b53b96613fddb65660dd336acc34a217960f6c22b9fe60dde1" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "b67495a863fffcc773021dc7865304" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810240112_0)
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
        
            key_len = unhexify( key_str, "dc2c5a020d3ea731362c29d559cb14aa4f8e3f6a554a5fee" );
            pt_len = unhexify( src_str, "8cf098cb6ad79e0f0eb4ca888da004dfe6431b5982bf1490c5f2d1486c288b5d50ea0a5a63cf9d097a71348632391b4bf962bf464419c2c971e76c03eedd09d069a070c86837e16a2c39a2cb8de3e2d3f274e03998a874fa98de0933b0856e076e7f575f351d7ecd024753781f51ef600405b304e37f326846b84692448d3f2f" );
            iv_len = unhexify( iv_str, "bd4d45d970085e0b2bfc9477f5cd0244" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "d44a4fd303e657670632da8dddb6a117f3e35d8afce245e7e6576711c663f36806b813ba6421ef9788681d9717a36d3eff4ae1789c242f686d8cf4ae81165191220e338bf204744c9fc70560683ec07c212846d257d924d5fc43a3d4297ac54428a32c8bb9d5137e0f4aaa42df8dec37793f3965ca658f22c866e259c80bcc59" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "9c1d6c70e1457a8d67f81cb3dc8e" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810240112_1)
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
        
            key_len = unhexify( key_str, "167cb184ab6ad15be36536f505ea5447fd996b1d9a092ef3" );
            pt_len = unhexify( src_str, "0b6ec08685b5a9d32346a5fa25d208136433204f3b86182bd1d9578f0634dcbb5b59937fb87df0679334d7f41eb8bec60ae1b87994ed2cfddcb56e95a6fb4e3ab7845b0294e4afa5ad46eb5a431cbd7ad0eb0473c42c06f3f62de03d10ddda449d41137c8010af5c7c0eac7a5fde5a39b5437a2382639fe3388ce029a7d4465c" );
            iv_len = unhexify( iv_str, "b5cc89a1c10329bb417e6b519091cee4" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "7ebe4a9547fb115b39b09880d6f36f8cd402bb798c6d9db036b1ebd8b87a8e9d56fc23b7ae4e8cac3500bf2f73952c37a068f1e472369b62319a8b1bc085a51fbe47e1c321dd1ba2a40692ecd68762a63467d5ecad66a3d720a8a81e02dac0ebe8df867e2f7afa367aa2688ca73565e55cf2b0072fa3681750d61e8e60275aad" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "30454dae78f14b9616b57fdc81ba" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810240112_2)
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
        
            key_len = unhexify( key_str, "9bc7aad4f4bd73acf756311ff1b72b41631344b9b57cf447" );
            pt_len = unhexify( src_str, "7cdf07e17f667227edc986827d55bb803c6e51f93e72d98a1cbd161b58155a1c14ca54d52254e5f88f2a08614df68cc37f6e9fac88895b53090f69544b18aee4cc03763d35e7dd94ed82d1435316e7e02367b1c43506b3ccd31e248dce81fe62fdaea3a0bfba03477d5c151b0f76f09799048d8b23699d000a9da11281efffc1" );
            iv_len = unhexify( iv_str, "ffa8e719f29139d12f741f0228e11dfe" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "6ab304cb9d1ed675383ff95f7f61ffc2aa73ab1b9a691bb84777b14c7014e986ffb91da6847d3abc0349a7aa09ed1d86f2dabc09e0e25a05800bd5d616c1a665bdb119ef71bae065ed019aed20ad3b13262a902f24ccb4819dc71419994a8b4774a3b9f4f672d31aaec997cfe340d2abdc3958c41373d0315076d22189eb5065" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "260cce7d5ed6a8666c9feaad7058" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810240104_0)
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
        
            key_len = unhexify( key_str, "5bd47bea08eab8694defc2b66e60da1be40fc1e398224f9b" );
            pt_len = unhexify( src_str, "083ad3fe9273b8063e144a03f88fb179b18327aba37259d7f8532303306ac9d18cfcb746cab3f9385b5bb685fbc4a252dda268044642f5dbe33ea6e1634445311e440c5507fa6beaed343c83aeb0ffc4f1cba21b39f0ff6edfff961aed3ae1796f8bfeebcd3392d92e26dd26a19a7b7c2e5910f22557fad600f8cca8aba988d4" );
            iv_len = unhexify( iv_str, "e45a52c5e5ecc87b4320864b38683777" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "8fa3cd91fb93a346e1f9595088c5503a840c7d7c33aa1be147e484e2aef2a8bda77275348ca59810abef6e179888f6781862990ba8e6d96af70febd2f671a3a8d6dce9be46c1cc6dbfaae35c35a7073205411cc8ab4ddd266b31b64edab4ffea076b29803149850cca41c857b05c10148182f8e7252e67069e7517da5fc08ee1" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "9fa3372199a2484f82c330093f" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810240104_1)
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
        
            key_len = unhexify( key_str, "850a811ca18044dee4487729e619cca71f05a5b164dd1250" );
            pt_len = unhexify( src_str, "6ee76712d0b1fc00e43c2312743a881ed95a0b06276c5a4d93e3d56732af6b12c7c0d1aa6ffaec562229b6443e576caecffeadd9a65b91efa1bfe48ab1ecc63c381d00fe8dc7f11365f2b28945e844e7c6ca60972f733a96f29cc12e259c7cf29e2c7bbf8f572e158782a46093c5754656d0f2e1e1ea2a0b315b5fa02dadf408" );
            iv_len = unhexify( iv_str, "6f79e0f62236790c89156c14bd9540a9" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "eb1ebd78d7ac88e6f927e09fecf7feb1aa64d7435aae76cc917edd9e0624a96e945df67648c187e397954da7b0888005f7a0d05d09de424c1a0648b56707b90da4021d5a36175500337e7341d1a474fbaa94e56d7ea52155829eb6bb9d95457c138875f0738034924d59681e7c2dfffb7dc0959697468ea2b65a884c897208ab" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "91c74a30e5bff5b2585ac7699e" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810240104_2)
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
        
            key_len = unhexify( key_str, "91469828dafd30de415067236d5f49ea14e813637f1ee0c3" );
            pt_len = unhexify( src_str, "e3aac00bd05ce3c9b87720db82104364c8ef6ef25d6f3c8bcf5f73f1a26f8619e831bf7bb28c4dcbac7013dc6282d07cc225bd969c582a26accd7cfffe878a3159a5ad3cb6c8b89131aada61e2960cc5431f4ef94394634e4c8b2938409bcd2e7668986c7c5cd2ed5f2c525fa0212996960ab842a43869ed430d3291799a2a1e" );
            iv_len = unhexify( iv_str, "cb5409aad9d227a3cf0e2c5f1031873e" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "4aa82b1c81a911cbe84231ce7afb95188f2177b539fb77de68f3d4801a2bd09f5ee2f7e59b5d9e79be5f7a23f0612ae39d59259dabc8b1bf7dbd4adc0db520bf7e71b988fa96d6b4dfc76afdc22ea31f64c64388dd93b27518b3263b0a19007405fc08645350a69e863a97dd952c8d886b5e0f444a6e77a9ef7c7de54f405a04" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "2a6b14c78bcb6e2718d8a28e42" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024096_0)
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
        
            key_len = unhexify( key_str, "7b6907853b7d4c4a19468111d96c5de048200b5441b9411d" );
            pt_len = unhexify( src_str, "3622ba252c067ce7d6cae1d1f5068e457a0cf93be01fdce6dc8652a53135d5ed445388679e3f388ee6a81220b19356b275878fbcc2a6751bee7e2a50adb7c430e4c8cae03e88465f97bcaeb151d4f0007bee6bb9864b33020717adc42d6f8a283a20f6b62ec79fb8060e3e5ecc1e91a2eaef57e9dabd3b3634236f12d4bff475" );
            iv_len = unhexify( iv_str, "a66ee64c15094be079084c89cb1739c1" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "2b8c1490e13881ab3bac875cbdb86baabe7fa30445bcb39315d057171e80d02aa8471355e80ba891b26d80b375508ba2756162cc688578be313a50096d7cd6253a8094970898fb99cd2967e78a57d12b8b3e3c10502634bead5bfe2c9dad332fcbda0c1bca16fd5cac78ebcbc7f15aad8b28abf3ed74a245a8e7a85cfaa712ab" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "e52af33988855d1a31158c78" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024096_1)
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
        
            key_len = unhexify( key_str, "fe63e247e8de838a197a9e937e34c0f5a0b282533d445015" );
            pt_len = unhexify( src_str, "17c5d748b8596901e97df660ca94fc970f7ebb769aff88f60acc425f50ebfb6744c6d8778c226c5d63653d9388d3fa0d4d630f94d668f3478c89e2708501edb12307a9b2189576cbc79388d291354cb9a5d1eace4ca1d9f734fc78e55ecbf86338a31ebe583cace752e8bafd0a820384136963eb2d2f4eea7b2f69597737a1ca" );
            iv_len = unhexify( iv_str, "8e018305675c287f802f28fe56ae5c4b" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "c3d34e2cf1c3ad629490d70a0fec1a63c88d025ffed46ff8f5d8c0879c166ad716b702682cd0a437bdaa03a9b2e69a32fb7259b0fa930ca7a344aea37886cc9850e44de0aa049b8bc300caee82e26b2a1e5ab45c4c7cc6a15f5f595199999a0cacaa59da1b2be2a204fe13005b92ce722a000228545ae8a61b2c667a386f431b" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "d7a6a917a286d8edf1289183" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024096_2)
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
        
            key_len = unhexify( key_str, "c739dae83a5e64bd98ffaf68b5bcbcd0155d8109e9ff2518" );
            pt_len = unhexify( src_str, "56dafc06b354e84ce3ce31b7f88193124ca7e7049272beb93fbedcb3ede8e017bdb9ee5d314ec5168443fe01258d9abc4c4c27580f6299b20082b4ca87eb2589bedc459f132dfaefafffdd13f82e153a2165dcab9a9b6c10f1d0d13b127312a6f5f3d65a73b8fd057f1d88038c5137254111f7aedf21af07a35e34cf4d2166d9" );
            iv_len = unhexify( iv_str, "d80ac4dacb0f1441839e2068013dde3f" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "9ae5107f4394c9050f8ca8ae6d1eb66099ccd0166f38e45c1cbc17b30e218fcf6015ac92dd7ab48bbb095a0523904c72710a86e50518d6aade269c82bc5ecdfa729802441e09aeb939abb43f5960542ad87961e2141f967d12f7190b07de99811b264dc62cb8f067872f84d21b661558ceeae4922900ffd76084e450650de79b" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "6a180ed4f3a9d5739e559d00" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024064_0)
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
        
            key_len = unhexify( key_str, "4c23ed64375d42c3a402fdadd143336d2f6babf4d4ebc176" );
            pt_len = unhexify( src_str, "5541a219108ce3ce593cca8c6aa6504c84983a98851bf8610d71f79a38bdc21d5219266ad56e10ccba4898ea969815ed0d6df75312d657631e1e22e46f727a499696399a0955d94942a641383cadebc5328da2ac75bf0db709000ba3277581e1318cb5825ba112df3ea9c453ad04d03eb29d1715412cc03dbce6c8e380b36167" );
            iv_len = unhexify( iv_str, "daa6f68b3ce298310bcc2a7e0b2f9fec" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "2a4e04101d4c822eba024dcea27d67eca7ba7f0ea6d5290ced9376049ae085ccae3ecb624c03eb5b2808982c88f0a5c4363a7271610b674317bbdf1538776f1fa2454c249a1b0d6c3e64bd4a356ac2aa2fd601a83d4fa76291f3ef1a9bfc858cc0aea10cff34ab9eb55411efec2a82a90af3fc80f3d8e2b56181630230890acc" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "d408209fabf82a35" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024064_1)
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
        
            key_len = unhexify( key_str, "695dfde34f0af192faa50244ab95a6059e2e637e237eb60d" );
            pt_len = unhexify( src_str, "33ca2c61a04467ad2bbd2ba8144573f0c2504a4e9945fbae250385406ed1757adb70534bd6ed854f227d93eee57c73a314f5955208e1ba5af8cc1e8b5bb07cb63030e3ae5f0ad287536f49b576418bb1d2dec40562f6bdda59c373d6668aaa9b791285716325fccbda2180e33955c8be19d05e389820ed69258c9b93e3c82e96" );
            iv_len = unhexify( iv_str, "a6a57792b5a738286fb575b84eea2aaa" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "b2ce449fc806dfb93cd7c97c018c2ba7d702216ae29a530a8f22d07279c7570c6288fc01fa9915b42a6be7a7d9569f71b8fc2411dd9747b5c9c7b5c0a592bcd7e8f4530ebaee37e9c7d48d7a56be7e2df1d91cecfd11bec09bbca7ce7106942989594e791e00e23557c843acf5164f3863d90f606ad8328696f4ca51fd29346c" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "050bd720de1b1350" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024064_2)
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
        
            key_len = unhexify( key_str, "1a89a516204837bc780ad9b26717e51ccf42591bf58c75c1" );
            pt_len = unhexify( src_str, "c72a1b8707522442b992cb21a7526dfd341e27a11e761f594abbfacc2ac26ea48523d0113e38adbfc06d4af8809cb606454467fd253ca442241e8921b906d6c007dd09e139e568194666d5da0b33c7ca67876856cf504e8dfab4a5b0a77cfb1883d532ef7c70b35b0838882f144991c25a2331863eaaaa2059401f61378750e5" );
            iv_len = unhexify( iv_str, "a9b1ef7744075cd6cc024f8c7b3b0b6e" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "0ec50150590bb419df0d6c410edfc2f8805a602ff247e3b50881ad3efb598ed053d8dd1deff86460db0081c0eb3effe9ea94564f74000166f08db24da6cfcba91a9ee1e98b8671db99edbe8fde11d0e898bb130e1b27358fc42be03fb3348af7de9376af495c0ec71aed56d680632195539b2d1d5bf804328d0928a44c9731ce" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "6c9f55e67533828c" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024032_0)
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
        
            key_len = unhexify( key_str, "4107d51f7d6e24aa605959d5d46b4c7e1743b7d5e3ae07b6" );
            pt_len = unhexify( src_str, "e5074ffbaf5e771e12f9e7cc8e7701b970aa7897928681383ea0f91bce8200ec6782dc9618e065e142c4ef2f7019791e74edfe2040b08bdf328d7d9658e7473beab65359d35ed168a2bb39f3c3f59890353405a82f48e16d388eb8f2145ed9bff016e725791cabca913813e7485f387223711c1ad098ffa0f72f74a048ec17ea" );
            iv_len = unhexify( iv_str, "94a88f6872995b26da39efb5e3f93334" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "bf32a717c945e1e2fe91fd38f3c7084210a7966cb83235d28f701ebcae6b2042226e932e4601eb3ed4728ca32bf7065fcdc98017dabcac23f0f80c65e92518db6c78bf4cd91f817b69f3c3a8891786d433f6c3c1a025c1d37bd1c587ba6004085571245591d615906f5c18994f09a03f3eef180d7af34f00ecfe153d5ab73933" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "8d43426d" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024032_1)
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
        
            key_len = unhexify( key_str, "0fa6270a44c8d14937cc3ff4cc2d2c997254a42ca8a09eaf" );
            pt_len = unhexify( src_str, "2252d1c4706cc46ad3e4f8c49a92cdc7d1af24deaf7b08ab7304ef804cfe261acc3a202bec0d8df42cc36a5a3ace9ed7a9465cdec3513d31de9ae7821f9444226439c8f98a9a7d99b36b91b1b00eac71080d7eb550209af5fb7b3f28d09f5060070da73a40456d60c0470773af95d16c0b33d0b5327d44188619b950590ea862" );
            iv_len = unhexify( iv_str, "b5f3fde841156bc408ec3de9ef3438fc" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "4fcfc56fa722af32e804dee0f4b67f5fea542b381bc47c41451844c82e5427f6cd90c37e088dbaff722d8700a11d5dfb4282e565f32e055324e5069931c86b62feb2cdf82ca1f62aee02a70e4e274b2b957650a5cc772be86c1b1cfc41b01d20d9be8b05b9e3ff65413520789ca0f198fe00d83483a1d85aeb13094c9a827e7d" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "1ae8f9c3" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024032_2)
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
        
            key_len = unhexify( key_str, "104c18bd2a0641fd46c2d7590d60d6d8eea74a2758ed0f4d" );
            pt_len = unhexify( src_str, "4434cf5d12d07614227cfc12716a8adfc651ffe5c6476cf4489afaa698d9d19947016bdbcb5b625773252745dfeaf9b10021a5b38f742ea8a0fc5f926c80cef6568ab8639cddcf8fee9678d45ad4937d6e6b054b65512f929e897ed5f965cd14cad939732c53a847bb2758d818d5d131977649db5b59a0c5ebac37db961f9d69" );
            iv_len = unhexify( iv_str, "2902faec60f754f0fbb1981aeba277ff" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "1789524845a1e36322c44dd1e938ee5d0fe6df244b751f3023d5d64d40a72598d352d9d2faba68be4e035c258b68782273925a94527fcdb977a41c1e0a96f53119b5909b23b0327c820e8f6da049a5d144a98019c4953aafd481190117573869109c265012a42f76bb4c3353f6613ccbc40a4af2f9e148bf0a0324bb43337fb7" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "d36d2d06" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810241024128_0)
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
        
            key_len = unhexify( key_str, "263451f187b6dcab9d8dc4364217a483dd80c1d75f24fcea" );
            pt_len = unhexify( src_str, "5e236c282eb4646fbd16a95eff2b27873f625a7e919237d75989a8a112ea80ce8db0b4aeaf5da59c3b22649dabb584284ab9673ba7edef59043eb8e99763643941a4788e7cf11bad63e13c9ef08644044b76beef68928dac22975481da4afc723b3ab3b498189542cbdffbc3f467d190cd02e9b36b6981122aa80cfa3aa3561f" );
            iv_len = unhexify( iv_str, "6c4552b3a03152aa464e88fd5b14356d" );
            add_len = unhexify( add_str, "435453a304fcd3c4bd6ab90d6ed8c54e6d21f75b9e56c9d48030499b04f6754cff628c4c9216f7d8a0abed5b8b7ca128c099a7deab74ecfe2c4a494b30d74833f837d254aa00d75aa963ce9c041f1916eb63d673a4af3f88817c65d4c86f5a3c28a67de2aaf75f08d1b628af333e518a7e99d980571db608407d3f447563f2df" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "12dea5ea9b54957c689c7c9c6a711e2880645109a4057fafe3b32727a60ee1e24f8450310d6b8402c26b307bb0bf3cb7c6407270d95590efb938e6d77359666b11a7a3833a7122697e959645d8e9d835e0bd39bdc30397115b4c348ea825c850c1e54a60a2522a6321e4b99fa2ad9215205628c595b07c6ffed939c779d23ab2" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "585677e0f37ae13d886c38202c3860b7" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810241024128_1)
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
        
            key_len = unhexify( key_str, "dbcf735d7c8701f537090d3dcf914c741ed783c24bd8265b" );
            pt_len = unhexify( src_str, "18eb70dff73341298ce33ff4049fa631f2c72c158fcdea55d1231c46c95ba4013012b713bc95ba25a2114d0380c297acd05c323696db466874083e18bf544dabffbc70be4649cfe7e8bf449aeb9789d6fa412a1adf57ce732702ab962561f9e1fa850733c97b8a4158786e8ccf32af0fc2b04907124e731ffaf3fa7eacaa64b2" );
            iv_len = unhexify( iv_str, "09ecced8460af635e46bc82450352be5" );
            add_len = unhexify( add_str, "cc5b8f82fce3797009fbd38dfad7055a5e2ac241363f6773191d0e534e2b4592a6805c191daad377245c414df8edc4d3d9ecd191a50cf9747dde65187484802e15797d7c7e1db49ea4e423e94d9ad3b99aea6bf2928ce6addfc00848550b4d2e466e85a282cc022c7c4469d2cb16151e81bf63df378e0c676036197d9470f42a" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "8298f796428faffa6085e458f149675d6c6e2cdfbc7994ee6f19af40fe8926c28904fd5ac0b9bdbd2de3f1614500a3eab1f980f82ac23cae80f3e6ba71539d1723e9f3412df345536f7517d847aae79a83ee9ad5fe38d60c6618d870cb1f203a3e1847d14d8de5295209c0e05aa196fec0eab8389e4eb66bdf3dd49d0800ffad" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "e53ca266dd93def5bee5daf70c953dd2" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810241024128_2)
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
        
            key_len = unhexify( key_str, "5f8d84908a8b7f5e118482bb867102a244bcbf48b7229115" );
            pt_len = unhexify( src_str, "9cd2a4e2acbeea6a73b5bffc1191d8045f63f3a14aa762eb776f35518f72bde4f9c8decd61a9319e3dfca82e682910a43de2719451e1a32839b29b27c3eb1c8f6118512d6a19cf189e2d04cf4e22459397936d60f7551244387294a7994320546f070e54f166cd7c243d13f3017b786f7df6a7fa4ece05a2fe49fc39e2225b92" );
            iv_len = unhexify( iv_str, "5ba986f5115d40c2cfe404007a1e2403" );
            add_len = unhexify( add_str, "06f98d4807efecfc863309f3bc64b0f04e4c16c32675ff97a3295d5657d4443f6c8b0a394d3f942705bdc19c22b8ff58e9b7c209b528b745fa0424d5898ef0e42e0909aa5ad0b01f8549e3674102ddaf4784f0ff8908cf9f9a25e7e4dd9af4da7bd13ae0cd87b6aaa6b132dc518f4a95af403e612edce63e1424dacf8e349372" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "2f168fd1c819b159739a7cc783ecdb0ef9639b7965918e343e2a55f196daf584f7f14bb6e42d37b504bfc2cc08c218c5b841b2d2abce05bbf15315f471e56f04f7d54d6f1dc7b7a68b8bc7026a1441105015bcee2c39d83be35d25f0d514bd1ffe75411b32867ebf2d532a766f9bfce9e55ea3e0240d2a3713ddc2ba790bad21" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "7f121ea36b36449e1db85e8a91ab16f3" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810241024120_0)
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
        
            key_len = unhexify( key_str, "f6c3037a59e98a9a81094d65ca52752ad92f93bcfa671821" );
            pt_len = unhexify( src_str, "26647f8f4092f80fc19f81f029c354c582b582516e8e27e97d50866e8ff755f50a8ae6422f4e996f0cf50826a68c007a5b16fd59002d368ed3285bbd04f8f9a5a524243cb8d5b3ffa184ba7384771bfc508f2e93abd2a1e7170d694d35cc0ff7f247e84ca8889efc820c3f6d9cd40afd56c5799972d7556c91cde50ac808652c" );
            iv_len = unhexify( iv_str, "43b4f15bbe525913a31a9adf23d1971e" );
            add_len = unhexify( add_str, "60826c97f0a99b88e7aeab774a3f2278f9d35b6c1a5fce49d9389a421543c99f68797224535dca4d7040313340da73982220040a063b045843a14f5d38763f95bdd26ef818f6e5171c8d5b47f183589afd6acd36e59b9946c1edf038ae285f500171e9850603cda36043c29860e75bfe03c21e0ef11a9aecc5d5c51bb2201d29" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "e58df99cce5b2548cf39684df6a26b8f9b7969168ff21c410bc40b763842ab3b30cbb3c82e0b420c8100da61c9037a9f112db9563a3d069cdf2997e7f4dbb0b5d79b56f0e985cd8cb70355366f7afd211bd9909c48b142c6556326062d27f7f82d76b83c433f00f1716ebc95038cb57c550b5810b77788c8bf1e686a8a14b610" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "ba6aa6d68a560642c266bf4469eaac" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810241024120_1)
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
        
            key_len = unhexify( key_str, "8fd9b08232a1d3fbe319d0897c74098f75b3e801d10e183a" );
            pt_len = unhexify( src_str, "a677a13ae26b7a05cecfd153aaaea02ccb50db601221a3df545164bb5fe638f6ed276d4bd172b9e740a82a690aec4f18f4f3a0afb80c9a32188958e1cb3651064f55ca1211647903f63013e46b46c7f4f95589012eb4ccd2451d8e8dacc3cd066281f1f0c71f69f1c49f3f94136a522fff0d02427e4bccab056e615ff6fde1d6" );
            iv_len = unhexify( iv_str, "304c759800b8e275dfcfd3e5e3c61a7e" );
            add_len = unhexify( add_str, "5d2dffb00a25788548ff1b2c94745e5bfcc05eeb11e63501007335d4bd06bfb3223d4682e7e83eca0e163d1a8f2a76096ab2839ad14b45eb59ea9b29feb76f40b0d8dac55247c65e5dbe6bb2d5155ddcf2b2f924c48e1c16c990b69ac48ef2350873c1ed524ce1b8ef6c92a11c8e461303f7c32b5d65b57154197e45f1c6b792" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "0779e5050dd17837d40fe3427322e717f074312f160c1951e5560797c13e4fbe47f320dc8053a39d2def4d3cc20e215978647d917ddf93fdf9eee5e54a974060dbac2a478afe5f5acbf65af4dccbd3942d130dddfd90cfc969da0c7f4b4050e34ce2e049c3bb004782abf4744c9a3ca2713ebfc5dfa16d011bc0b33d0368c108" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "54c8a1dddfaa1cafbcc1883587b4cd" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810241024120_2)
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
        
            key_len = unhexify( key_str, "19d38467c1024611433a0b2780980538d88f3e8907a86e42" );
            pt_len = unhexify( src_str, "2623cd0eb46a7366877149ce0204d7dc08a5e64a1adb3b6759178c4eab26ca1806fc25fc0fc99dfc77d1811e61ac1e04ee82eb69ef7527962df1707734e4aca970b8a499eb70c2b0386942906945abcd9234b92e7bec33009e70786c39bd241da3905d961473e50367cb7726df8da2662fb32101d13b75032838f01ad7946670" );
            iv_len = unhexify( iv_str, "8d56a9e4bed67a7eb0f7b8c5e6bbf04e" );
            add_len = unhexify( add_str, "1c7d2744a56f5185b9cdf14eb9776ffd315214540daffc69c217dd64c7d0fb4a9f7b1ccc4c1e325fc046eec4feb8df35d32f492a28d35858ad1e9bfaf95211f111473c2ff799a124b308fba996b08f185103607605922bad319c6b7fd211f97c861565bea34948bfd751e4ce2591ae777ab1df8dc9e820cdad13066ed74303c6" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "edfdfa35b41c5642e5b4dd1769b635811a48ecf21915cbef3c9e2f8230953f2ed4fda8903ec7634f10d55aa58c975a6c6133a173c2aeb83d6d7fc6534ea1781dfc62408e7a17d255a983bd1c33d2f423c364893db8128a599cd037b2db318f86f1fbd895a64a9fb209490b7e9a30db8cdf42e348cfcfa7984e15c17db810ec19" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "17dff78f61d21de4c0744e57174f70" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810241024112_0)
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
        
            key_len = unhexify( key_str, "d69bdc9d35589e33ea9c2b956780cd9618e0df79d1083e69" );
            pt_len = unhexify( src_str, "d8a75de22fd3e2d50127c6fdeabc09fab1178875998319e1ea83c9745a1d5833c6ba9df08177c349dfa412e13e1927bbd4cdfb54a21c86c149be1feb4d9de99ffd590850875a3d9c8d9306971a9802ad4ca48f73d0517322525ac8463e3d59ae9895c9b363b6f0728d7585de78cbb49757bd1919ba2f2d6ba042d0781e7a79d7" );
            iv_len = unhexify( iv_str, "abd4b94362501b8f307fca076fccc60d" );
            add_len = unhexify( add_str, "1ad9aa99a4c8158ec08d21ebfb62604a043fc0c248fe08daa15a89f4a7855916af8aeb681ac6600c0268ade231f918fe508f48c9cfa998effc350fa117e2772f04839f8fa1a53bca00693ecd28db27c6507750404bd89af07451d66fb7dfa47065e9d3fe24a910eb27911591e4f4e4746b35359afada4356676b3c7331c610ab" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "52e88b54b410dbfb4d88092df52688ba9502b906752b4802aca5428437d795de0d3539945bebdf2bab070df4a7e630469b92fe2dde0998d44094cae7f21f84ea7806637fa5c73b138e87d04005ef1206ddf30a21f46c0aa718665e809ffc0b42b5250143604b20225ec460defaf554a8bfb5f69ef4244e02e9563c374a44f0a9" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "1024f8e9997f5fa4684930d17431" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810241024112_1)
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
        
            key_len = unhexify( key_str, "6960be8fe82061e9cd783cd1c03f63a00d60ce9fc47ea496" );
            pt_len = unhexify( src_str, "e0f574ddbb04831b5a86f40182f5f10d8667fe13c7065b471df157f67230c41b8c069c0218ceab93d63964be8ee853c567add2c3eab1670b03a51f9175e8e763be778ec43833cd716e1c8fe5cfb1d663149b21e06df772a3973fe1297d65188201cdb0c3533f5e9d40bb0664a97db60fc99d7e48eedebf264024006ca36361ac" );
            iv_len = unhexify( iv_str, "672f4378862c82738055273c72555b39" );
            add_len = unhexify( add_str, "e3a4dbce87edac519ce86349eed2dba0d371cef0d8f20b4dda3e1cd9f5799c9fd0b7494daec5bc995a6936c501212eb957ccc9ddd4c9b8a205cac122ba87b5c5a0eeba6b2af2cbc2326d953d61d089b6334ce03257203072f8e06b8c6f37692748a13e681082900fd32f0df6a3072f3a8b9bbeb3ea558e97a43d6656093d7c75" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "2a3c4b79bbcfa4af04baa8413f6f1d18c9c579060ecd0cc359fcdcfc0566697ff834f7dffec84b2292e8583ecb59c9e5e5d87913a6ccaacebf371f1fff67f0be749d4ea5f5c6f4c959e9d932414a54a8e25bf2f485ecce9e70990bbc4e621ce2c8fcc3caa66b0730c6639de1bfa0124afe351912279bc3ca363f4e6013496cf1" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "dbdd6af194f2578a0d0832d0cba1" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810241024112_2)
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
        
            key_len = unhexify( key_str, "2b7d0115612c56a1f28c6b3cb3d51c2b4bbd4cd36ccf3dda" );
            pt_len = unhexify( src_str, "3a88efa524a90b31873cba177a7e6e050dc59f42c934923db1e75fec924908370ad0c9c3b0b3c05adf12c6ef2627d8d16f832071c055aef5f581a39a8e7d9bed2629e26d5e3ecaed24048d744fba08d8d12132def62059f1a549044c1db121f47f10b3dc4a02849150aef53bd259d6814162761cbc9e1a8731d82101696e32d4" );
            iv_len = unhexify( iv_str, "317a60c3c29440b8ba04daf980994c46" );
            add_len = unhexify( add_str, "80d816bf4008ae51b9dd9a25c30cd7482f2289f5616c41d99881aa8f78b5efff84efe307a822174f3a5c08b381bc99b169b92057627f21dddc367723eaca2545ce3a4fba2b4633fd99459fb03e85d6d11ed041b63185f3b94f6004bdce556e2a0aaf811faf0153b3974d0bae3eabadccfc95474c940ecad5b4d5ea88f88b8c4a" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "f193303bb781164e42b3d4d25569a446c86646bc0fbc93059603c0b46ec737ddfcd55df8c90e6d806bd9fef90f2b122a1758bef5c75fcdff95ce44217d9b6b0e75e77656cc7f8a8cc47729c74faf43cbf08202e9ad16c7ef8c832ce5f97f51153e178ccc3c168928f3c328cd5b4c341bb0482f6a292cfa2fa85e03d95bcd4cb1" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "42308ffc76cb6ab3c770e06f78ba" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810241024104_0)
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
        
            key_len = unhexify( key_str, "75737e01a95c2ad9c860e72a57da646e01c2286a14dfec75" );
            pt_len = unhexify( src_str, "fa749799afcf2242a6000c4fe1e0628db53933dde99d672e3c7b24b0cd6533b8002bb7aa8633322f4ee2e343db3a0067ad44edaa398cd90ebdb50c732e8bf95aceb4aaa4dfd1eaca617c30c30c1a18761a6d24c2de0790f54f73e0802eb82ffc0124517ddafe8336f4ec6197219346deef4ce930e8ae20117e6ebe49a2582346" );
            iv_len = unhexify( iv_str, "1060d78543be384e7a9dc32a06bcd524" );
            add_len = unhexify( add_str, "528a6c34c3cb3aba402b856dd7c9677d0d88821686edd86287e7484b72248f949bbdfb640df27e3d1d6b6dc1293ea6c84be72c85e5ff497f5da74d796a21f2513385a177f29f2154b2362d5ac83c3897f368d06513333f2995b701fb3e5aabac559f6018fffd02cd6b65eba9cdc629067f15d1ae431d6a22811508cd913009f8" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "7e8774cb73468ad9647f6946aea30e9468fac3850b5ff173c7b346891ecda32a78b58df8d835328615f36a12c18370f3abcf021ed723830b08627767272f769a2105e4786451db0512027ce0e3f770fbb0ee0e1850a5fc479df4ad5ceff4fa3b2b9124c330c2e79d770e6f5e89acdc8d0ca9c758980dfefaaac41aaf6d472f8a" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "6bc6632bb5b3296ede9e1c5fcd" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810241024104_1)
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
        
            key_len = unhexify( key_str, "a326226b24222b3389d793b61b723e9ac7059495a1b597f5" );
            pt_len = unhexify( src_str, "1cc26e453a54c94c1cf902fe12307cce2fba4d5f0fc3bb63cdbac0dd0b5ba31d08dae2b4f054c86f3a3ee920d8b9f7ad8ae8b4eca090c8783cf35db5de3b95889a84f09ff3f70263c61681f00a454b0813813f0fe3ec38a6d30cc3c6a93c91a422743e7a72340cb012718b8a4a3b66a75f13e0165aa51ee4b00046cba12e966d" );
            iv_len = unhexify( iv_str, "327972d0c2ebc20ed5bdedc8a3a7aee5" );
            add_len = unhexify( add_str, "2edb1455bf4573a54ab921d31b7fc9e534bce0870eb6e973afccc3b1f93dd2c1a476dd88e705919caeb5d4f4a8516a718cff8858eb443ca7785579036cc7273570e7bf2489ce71a52ad623bf7223ce31232d8c9b18e52a2dd4519bb08d87301f3ae69dcc36c6dcb3b03d8fc39b6621f6b4471092e941ef090c9821a05df8575a" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "5a219a0d997e879ffeb548d43de8e4f32a9ad196dc425c83f766134735ad2c9ff5d9665bd54ac3efdc50bb4a7a04ba59825f31a0f3e530aef45bba00cd6479efaa19c85edb4734f91fdad6686e50f9cc531fcabce9e8397381b4d691da4a27b7c487e93de3e3a9e769e831c69b07697e4bab470ebff628e710efa17e4c184e0f" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "2b9ac273c059865fab46f05ae3" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810241024104_2)
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
        
            key_len = unhexify( key_str, "cf5f2d843042ab94fc4519d57d9229ea7e8172acf515fab7" );
            pt_len = unhexify( src_str, "0e20f5a2afffa4a5f9219320716c8a26e35a19c525dddd528e5f5f06f0da082f43272361f07cfdf57423f67ad3cfdda9cf1747c602a93747cc571adfabcc9d1ec1a8128908df45fe0ede0e14ff40169dd1ecbff7f4062ee7be0a1afb370c9d5103132c1fbee9262309cb05ea150fa862d6303af71677d2de9cafdb4ecdab8d5b" );
            iv_len = unhexify( iv_str, "95b06c3ce1a3de73cf51e781b941097a" );
            add_len = unhexify( add_str, "765c3fae74b6fa4b6ed4ca7ab9b829d76a7759c50874a38d2ecfddaca2365f7a143c9584e255608be829950393e5f94131caf4caa04aeeeb9d595e39ef3f9830246d6066995b2d40438f7eb0944bd452ab493b422e93a3e0dc3c0fc2a4b83711ac6693f07f035fd9d031242b6ea45beb259dc0203f497a77106392e4da93c285" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "f43628a227dc852e0ad931e23548fedfd57020a26638ceb95dc5601827a0691c44209d14113da56e6a1e44c72845e42ebbc7ffbbc1cf18c1d33ca459bf94b1393a4725682f911f933e3fb21f2f8cd1ac48bc5afb6cb853a09e109dadcb740a98e5e7ec875cea90be16bcdfec5f7de176eeeb07a3768b84b091c661f65e2b905e" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "77964b5ce53209ee5307065d49" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024102496_0)
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
        
            key_len = unhexify( key_str, "11cf18bbbc1d8778faf40391c30ca417739ff8e2a521926c" );
            pt_len = unhexify( src_str, "a2e11ac093ab648118759183cd52ca7d5728ca87fe2f31eca28cfb13325e3e6e95974456857866dda78359023e2c998d2c93c6dfe8f72c6d4ac39ca0585a53fc074bf1124c3ada92e78462a445da23e650bf52e26b782ff50312ee2beb7410e93c8435f7b88dfb0ed63d9a3823992d796bf3ab147c33593c5e6193ef32f7a620" );
            iv_len = unhexify( iv_str, "bdd9a2b70e4ee0cc501feca2a5209c3b" );
            add_len = unhexify( add_str, "051c68fe0cd81b52fede137d0105e69c74771b770ea9b573ad92ecae86f420953f459755597f68c29f6fca39a27239faa940ce6c949ccd44c9f12a0160cf74a575753310f52ec5c5bb9c4474b85266494e63b6810ddf7a6abd1cf8244cebbf69d3198c4a09e8dccbc9429f81791f5c02628e9477b988e2bd10f9bd5d6731ad01" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "ca899a00654730d68219ca2ed9b23058a5f40150c237143b24245de1e440329e513690f00c0c52bbd0de8074fe5d7a50fe420470249227f967340efeeb64c424881c7f3a20c405d58ea81f2309c7f74ae572b30313e2d4b419fbf5f2cf90c6706a1ae1a800a883e8b00fbbc9dc28bf5aa4a329246bbe94df5c2d4524f57370d9" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "dd45503cc20493ec61f54f01" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024102496_1)
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
        
            key_len = unhexify( key_str, "812481f8982b42b2fb86297c4b7c739652908dc498171c69" );
            pt_len = unhexify( src_str, "32b27127582ceac21f968f5418e24ec8b84617f4daab0eb007f02d45812e81d486dc50909d79221c438def535b8a55946f50297963139a6b21e139e810d19bc1566b374d080a387a646bb582dc738c44156eb6c8dad613586662418edcbb18fe688d271108318de71734cb571d442e4d9537b0fcb2f5c763b3fbcac010f5c4e1" );
            iv_len = unhexify( iv_str, "0dad658c73c9c88dd927a502d7b14e8b" );
            add_len = unhexify( add_str, "af44f747d77a83ef0944f3bac8e835d752bb55772a7fbd3c6af27ca0eaadd122c9af1e2a9f37c2ba42779ed8cde2199125378fc88c7d6d58edc01c65491c5efc6bee58e7e8bf72f1a69d3dba47b38a50077130cbd71accd3dd4f193a53c6f2d1df694476767f79f8b71fd42745ee5bd41e90a7dd50a1597445251b32de303169" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "003ae4798f6a0b97990d41373623e528618f9299cebdb0d23e3799f62bb063e5530eef7fc40c06af16965ff6895f675ffb81c004d826cbd36b5eec9bd3d90d785af03b64d12d311b6f90bcd75a40497d0fad5e54f829a097b25f7a9604f6fad475c9239a0f8d5589b8a76c6f7bc852a3b820734b426f59ee845ec3f09dd7d3d1" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "b80bbc002cbebfb4ec5d48c0" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024102496_2)
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
        
            key_len = unhexify( key_str, "a6657a7a9ddc6b4595df94d7c6bee9d13ad231cdc46ae5b4" );
            pt_len = unhexify( src_str, "36857eccb5b3c220265a40980e8949135e840ef270602940d3394f3f679aed55217c1de175f6b48a16f7b394ad7d288bc425762f971b752d1372b369fb1c3a64970c8d18ad6de2e1a9a561a749e3cf9a8524e239f3121e8643bebee471e55fb5d54a3453c51b1747defac98ead8b25854ed1cae7ac44fd28cf4b1ed8988875c1" );
            iv_len = unhexify( iv_str, "68621ea7c6aaf1e86a3b841df9c43aa8" );
            add_len = unhexify( add_str, "bc25c38d3a200fc17f620444e404f3b3999f51ed5b860c04186750f55cc53c6423c44d0eee02a83af27d16b9652a7cb3d34a5cb19694e5399a272dacd56c4b17872fd23fdca129a4299b9c87baf209dd1cd1f355088e3f938e6d5053a847b5913f0b9135d6f290e365508bed73c61160a11a2c23aaed7551b32882c79a807230" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "de8bb8e69f9ff1322f0a6c30cba5a6fccd7d17a2173a86cff5478ac8ea4ad6f4e99ddd4149e6a9b24865cc8fd6394066e24a556f3f6d48c599592c56f06a946c6b3414e2fb47e14d1f128ef26b385912367f35082099c1f3d4ea86687f19f059c56dac21923e9a151567299416eb311f5bbf9a28968b080b0b200044668f0919" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "065f6c2b86891c719ea76984" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024102464_0)
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
        
            key_len = unhexify( key_str, "20cf8c2c47cd583286157b45b575d4d69c793b4250274fe4" );
            pt_len = unhexify( src_str, "a64c2131c94fa827c3a510b23b20fb6d04579bc292d2ec33efc9eb31459115da143f73fba9bd8b03b67e591497d108f900a7279542b480bd3a13ea588a29efe66688b52c3fd58598c66d8595888e87b27734e6c5b2796cc60ab2aa3dd06a29c577de5bdbf0b6c69c7034f0181050f286b9300d214f549165a0b5b56ba8e40641" );
            iv_len = unhexify( iv_str, "ab58d2e18eb83c20df94cd6b569c65fe" );
            add_len = unhexify( add_str, "93ff6057eaaa9559d87e3276d4d900888cb1f56434ce2677ee1486a0aa8f4e8d02c47d06e6841f3fbe5bd72dd37fa9d81bbef807dca6961910844eb9611419936310d717e1843e7b278f48ae44a57c1f227a75fa8cbc7fd57c8cc3b101e036c8ef3043642c81f381561b61da7c9d79b6da9ec46f7cf093c29c1930b27c14f991" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "a3f621261af17ec4756245414280017fd36133f2f9ff89eb8979d4417b8f93892bbf7b08bab785341bf0c7b5e3643f0e33f036633e5ebeae7a750ffdfcfbab690291731e92238ba6b45859b309629224fa7efc72298d3cf1ae3b6a9e94797552afc4e3a46205f9bab7eb64e4a41aee0e45289704a97221b7118d209e0b267a68" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "ae53564271d5de5d" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024102464_1)
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
        
            key_len = unhexify( key_str, "8a311bf356cb1d1f58eab411b45b8d78b88052f3c8ab821d" );
            pt_len = unhexify( src_str, "3e915e92f186fde05ad55a2597ceab81495abbaa0be107dbf6a375525d1157a322b1f65460dce0c3aa2bc08fa89f777dac4d2fc3e5f7f20a0d5e33373c7f1c3551369737124c702928726bd9db96a33bacb56f1d645fa02ca1d88629c547c0eaf9585ee23b530ea971bf439c67e3b752af882668ebe0c739b26c837887b9d2be" );
            iv_len = unhexify( iv_str, "0569d05f3825d16aaa89e86812f80628" );
            add_len = unhexify( add_str, "28494a12026eb89b46b6139573dcda0836a617e00e25e2daa92f9372d86c3c162cfec34d634ea48294c784825615f41e06e555cf916983931e3d6a7ccbb4448670139616e3bbf7109387a852703b0b9d12c1fbd966f72bf49a7e1461ca714872ccdc59dc775c24a85e9864461123710fd8dcc26815894ee8cf2ca48a4ec73b3b" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "9ba776653e8d9d240d9c1ec355027a18731c500928925e7c50ef83c6f36957073a8386ecbfaf430634cd557b1da1bf122f37456fea3e9b58a6e99413d9d16a2f1b40dff843fe16a2fa0219ad5dd8ae4611de53d7aabbef7a87ce402e62276addc7f44e09ae9e62c5cce4ddce5695db987084385ae9a98884ec97e67b549ad440" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "c669ca821b6ef584" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024102464_2)
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
        
            key_len = unhexify( key_str, "82fc47638cfb361ecf7924c03925d6006cb99459ef5691e8" );
            pt_len = unhexify( src_str, "d14a550d419b8e03762429a7abda3b17ad7a1305e5fc639e71538285cd70d78fa30e0c048e2c32d2a7fd7f82c874d63ae922db5a77111b46caefbfe4feef4df19786e5fec6a4df84f76df412b1f06bea149f1996b41b117d00d422bba5566d3af5289ca9377f325ca1e72f7d6a32df6607bde194cf4ac52c28e8aa1e8f1c9a67" );
            iv_len = unhexify( iv_str, "2a8e1cadd2f264f2ad7be9e7bdfa24a2" );
            add_len = unhexify( add_str, "8088358d7c3ca8951d7e8cd6cae15844edabccc8d0fcf8f169a48cf434d4814f1d7d9ae410e5581d414f952f52b852eb10fcf0f2a67bea826ea2e28331f0597152e624e148462d5952f10fa363666d57ebfe036695e1e68f79161b991e20c8ae6095232e63fde073c79603135849c62f8d98a1d80608fc081171114db41883f6" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "e54cc95e845f4d1b28885e9b90d1d9d3cc51fd9d8fec9bce57de8781a28b4e5b7ab446074e84471d7a9a23748b689c354e402be77f9890a9c52a2eb9022a6a415e01285db1c6eb66d5e15f4216a4f3f45782677b6ccbf20ac7b35bd153f52a599712d09712ef1454ccf72ee48cca967f4917f1aeaeaa6eaaf8493ec7ff2dc1d4" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "093343e49b70c938" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024102432_0)
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
        
            key_len = unhexify( key_str, "d3180703e1ec93b20d1ac4d64e85d5461d75f783bcd2f4fa" );
            pt_len = unhexify( src_str, "b7b350db6fc0796e9fd0cb239f561bf7e27b2aa26b8e3e76d8b737caa1c1c5ad624a32f5709e4b751f8c21172d4d0f4ba38ca4d1d0e2570c084cabdd0e8880b35140c84f775c3c301a9b260825e1fd75f9835777d6c0e23d359af1a5f7caef934b91bee521531582b639be2cca87c2991f5525f4a2f64c30a1453013d73c16cf" );
            iv_len = unhexify( iv_str, "916d72d515d3247ba48828d4113bda3b" );
            add_len = unhexify( add_str, "1002513035cb1d7e8b2710ff8c93cec55e2e2c2b56000d4c1182b5286736acd2d6f2fc9b82f71156dba6f77463805627e4bc38c96e091ecd945df7e996e7fc3bbfdae3d85ef1337fbce960fd1d60d06962a1669e9e8d20be151f6323cb38ef68ab5e838f02a0f379567f518f15d81b192cb25a42356807c1b9c02bef8309ff44" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "d590f2afcd64c574ece12c675f509efdffc01e1404cbafbc923c4b44390eff66dd839e6d800df67bc06f49f76911df3cec36a3a1521762d6d4a8ee602ebefde0178863443f918668fcde8a531f3b5ee0e4c379ecf3e75e7c59f753e41f4e39811bd3e7dd3d6bbaa1e81fdbf8bd976384a6c4505f7e4270321c7329bba7f15506" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "22e50ed0" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024102432_1)
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
        
            key_len = unhexify( key_str, "02bc0a8ab5468123009b2c69aaffd0a20a1fb082b55a7ecb" );
            pt_len = unhexify( src_str, "8bf32af1632a7903f00e801ee6e5c690147c021be6886cf2462b2172786ab296e0feb96648e4a602ae6dc45e2aa60e6610356cde26b1dde3aa114c5449216a467fcde18332a6d482d24a1ee952379340d036a48b63efa092db4c30a95f402d57b9c837183e900b47805f170cfe9e69baea2b939799e983f7470bb1297f937bbf" );
            iv_len = unhexify( iv_str, "bcfc15308e891f32506a50c4ed41bff6" );
            add_len = unhexify( add_str, "01bff5e606a536e951213b23672db9074fa8bbf947e815d32cbfe30adc1e736517f86139840a4aa0a671b4e9bbd6a59d292db34cc87742c0dfd2d658ef157734c5fdebb3e5772d4990ad1b2675c23ddf1472e892dafe7bf140d614c50cf937923491662299ab957606f4ca5eed2facdc5c657784bac871fab04d6cd3ccb18332" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "b8dff03141832970c925e7ff0038394a0df7f35add3046cc56f73e3eff59e18932aac697456107b6da7da3249049c3be5c098dd730cd4bf68cdf798c3a932b2c51f18d29e4386cbf1b7998a81b603372337784307b0beb59235eba4d3e4810899f6d71a994ba9742aea1875878ccef1bf674ee655a0720bd37e44b33cafe5742" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "bd0be868" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024102432_2)
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
        
            key_len = unhexify( key_str, "7c07d5ccaadb9e3ba5b5ddf380a7a2a175522b98e31e1d34" );
            pt_len = unhexify( src_str, "04d3e6bcd5ebf696fe84a702ffd5f76dcbe9679c909b36d41ce6362f229304aeb19896c6376cb3c25081f709af57d36f39f421ecdb70bed9f829558bec6e78823275fc11f9a2d5f773d27136d903ff08e5926338dfdcbc182825794e5f739efc1f0ecda8e53751edbe0d08963471fb5099f2ff31f76b479677bd6d186a409525" );
            iv_len = unhexify( iv_str, "e4db5c6403a03daa703516763052bce0" );
            add_len = unhexify( add_str, "b747d97f263d0ff6119df1b5332640d2e4568813adc12ed76175fdfffafd087456748abb267195688d2db41caef301117979dfd2db9714b352398594005bebb449ea20415fcfb2671253f69bf6467ce7f5bce350a834c4586eb03e766c1930e7e6ccf01283ea31b8c73d7967cde0f2a53cc46b1b50c48649044d6f753f1d54b5" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "f5faf7bdd99c62ec87f93da2ca3ce31e694df0a0fd04d61914f9a7a4235de20e0a406e297ba1099fff8c14e8fd37a9d6cbe2c5c572c988cb1ff87ffe7825e1947ea3da73b8b3633721fb4e08deb3f8fcae2407d73bd4c07f32b4f9ad0b1364003b11f84037a28239e96c3996874ba8e4ec7270bf0441d648f52f3730d30e3536" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "e0820c4d" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212800128_0)
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
        
            key_len = unhexify( key_str, "dd01d48789ef7f07f80a7385e4d1b1734903bc6ec768c9f2" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "944ed7743be9ce370cba7b7c9b7dece2" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "dfa0ab389c3a780f598af80200c84da8" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212800128_1)
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
        
            key_len = unhexify( key_str, "0383849ed0db3e52743aa82fe8cd9173b457755be8bbd46c" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c6b8518346ec52c001697b7bd38dc795" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "48a1992549b627c8621e8fbaadacb16c" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212800128_2)
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
        
            key_len = unhexify( key_str, "936388053ee0116b3f783ae34f000d5fe2c5d712842d46f9" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c5426b20c014e472c7b85be2ed0f64c8" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "4cf0f6a45f3544e3d391375c8fe176b1" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212800120_0)
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
        
            key_len = unhexify( key_str, "40dfcb3151a8dab1cb79a6a1e6a24fb55024d0e256bd4b07" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "b8495cc54653e7ad74206153ea64c3cb" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "1d3786412e0ceb383de3898ef2cffe" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212800120_1)
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
        
            key_len = unhexify( key_str, "83ca41d8b33c6134a14d8b30b0c36d5b799574dd925f3b8b" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "fb9aca5b4932035c65b571d170fdf524" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "9787f7d68d2648963cb49fd7459121" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212800120_2)
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
        
            key_len = unhexify( key_str, "886e646688d573c2dcc8ca229a11b394b3400408dd801503" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c0744685722cb87717c76fd09a721dac" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "794fe4df0084c21ffeaf959e5b0382" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212800112_0)
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
        
            key_len = unhexify( key_str, "0b845dc2c4e9e5a94bd3e8969300b16b45d3ad5eadb2e80a" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "0900b3fa3cc9833d702655d285f904ed" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "dc670518e150d326921bd5f43e80" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212800112_1)
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
        
            key_len = unhexify( key_str, "ac9fac2e32ab44a0774949d53a62c1cda04b132a3b07a211" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "8cf6a81bfa21633ad95ffc690c737511" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "4cd7a6e4f3ec3d41d086e6abf14c" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212800112_2)
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
        
            key_len = unhexify( key_str, "9f9721ef784980d03140490f760313cc8a56424affb01672" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c104bd8482e3fe7359c85e0e94fd4070" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "3f682fc71989804ba74bdad04a97" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212800104_0)
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
        
            key_len = unhexify( key_str, "f7c935f56970678ab89f6d97315a33efae76148263e95f1e" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "1a91965c5458f4a1fde309cd42a3f277" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "ce266c6f0447623a3ef1f6f57c" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212800104_1)
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
        
            key_len = unhexify( key_str, "30ecea6cac70a9de4f4f7f441d6b9b5608cca39d07c0ded5" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "361e5cd21c670de39b5f0b2b89437f99" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "48a9621522a98bc6c0acf03429" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212800104_2)
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
        
            key_len = unhexify( key_str, "4fb80c4fd026c3f68ab8fcb8e28e144fdb3ba00d70295ebf" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "ee552fb94a527d18d285d6c195ca7b2f" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "5ec97630ce593e9d560136774c" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280096_0)
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
        
            key_len = unhexify( key_str, "c0261023ee9f682789ce9ae970fb7601f07551259ef91945" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "bffe4af76db75bc4a3d42b57c73c51b6" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "bf827b4526da77ab2e21908c" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280096_1)
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
        
            key_len = unhexify( key_str, "4fb4ab2071bff4ec239ac05c04800806df2c256a4845b13a" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "3ee0e2e72eea437e46a873bd659b1c4a" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "572d3ec2650ad57eec84fe00" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280096_2)
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
        
            key_len = unhexify( key_str, "193d5ebeb466d3fe68754bba413c65112ae29c5ca5e450c4" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "04e9d31b3b1205cae37c435d5a5579df" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "71004356f266688374437aef" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280064_0)
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
        
            key_len = unhexify( key_str, "9a455ea1d9a78425a41d43e293e88de40dd6ad9ab2a63ef0" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c108c56a1b3261042adc89046ad1ecf8" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "213d77ed0534cc20" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280064_1)
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
        
            key_len = unhexify( key_str, "d6fff8797db2f1884b7d71e3ef3e5983234a837dbd0c2cd6" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "6be4417d7c00fe9c731e0932a7037a71" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "68b6c28786a017e7" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280064_2)
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
        
            key_len = unhexify( key_str, "86e6c451ea0ecaec9e365bc4831e7a6c092b65ee9bcf1b86" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "6258168da407ce43cc18d6931497c1f3" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "cbf20172e75a6316" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280032_0)
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
        
            key_len = unhexify( key_str, "9295cc6458d907da5e7c356a7de51eb8e8d3031f72a05fb7" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c7eaad3389fc24a4ef96a711ffbfff9e" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "12508e37" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280032_1)
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
        
            key_len = unhexify( key_str, "308b6ee958f81a7fbf3bc386e167459206df9c1cb999d904" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "2c61b991ce478d9aac818d7aa75ada36" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "32ead170" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280032_2)
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
        
            key_len = unhexify( key_str, "873d033773218387690c2871448578d8440ef36553583928" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "02072ec745c856c6e86873a0523d603a" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "e6a5726b" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212801024128_0)
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
        
            key_len = unhexify( key_str, "cfd9c1375dfd19e64b5e4b75022fabaa049627d5238cba3a" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "0a745c6910b23c78b1b44c02f1ce11b2" );
            add_len = unhexify( add_str, "0cc6724b9f3675619fbc70117bfcfb5871e903b0f01382e404793c1dfaff5a5b4131a7fc3041014941dc2c53871bee3ff18c08e9abbb13a8ea220cb89cf65bea1581eb8ac43d148203532dad8854616210ed7f1f9467e6b22071ccc8bb7e3bd89a0ed02a7058401aa4f2b5d0ce050092b650591282e66ee789bbf032dc105503" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "8ec41e9c76e96c031c18621b00c33a13" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212801024128_1)
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
        
            key_len = unhexify( key_str, "6c9f16c5dff4bd8d1855995dcad1c4253759b6e2a833995b" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "3f25e3210d6d9caa8725eb55c6813cef" );
            add_len = unhexify( add_str, "7c6a66d930c95ce1028310cfa3670b77ffeb5e9b627a667859665c1dee8e69930c287fb1f1a3706ed1a0d35eb6d1becb236352a226a5b686bc27e1e1dce4ac6d5974d88b9812b39ba289b2490821319b5fd677da23fab3adbae4fb3630e2571ac887ed951a49051b0cc551e7ebe924c0cbb1c516f71db60e24773430dc34f47b" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "5e000478b55ecb080c1b685f24f255a9" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212801024128_2)
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
        
            key_len = unhexify( key_str, "a8e393e00714cd94de0347351b924ebd555003f3a297493f" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "9c7eaf730fa8074acd372fdc53b726c0" );
            add_len = unhexify( add_str, "ce4cb46e67d85c5e68afe61ddecb1a36da4de42774d45250d0d52b328834385ce1ceba96f703daab81d7a981cd80c469855e14d834df41e4c0c98873f3dbb777fc0562f284c466b701a530f27fc4e6838cecbd162db34b8e8a23131d60d1f9dac6c14d32a2141173f59f057f38af51a89a9c783afd3410de3f2bbd07b90a4eb2" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "66bb46adf7b981f7c7e39cfffc53390f" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212801024120_0)
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
        
            key_len = unhexify( key_str, "bd356a8acd12b06de9f63825e93664cab1beae7f4112cc70" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "72eaf459b8af0f787e91d117931e3cdd" );
            add_len = unhexify( add_str, "9295b227be3e1faf4e969be6c7f20d507431cf5da9e2a577c9b31538058472683bd52f0ad3f2fa9f68159c1df88e7dde40d6612f8abb0f11a0078419b34b558d9144ea6596a09e5d5548b275620e5a3096dceb2768d2f77a0b79e0b963387d3016ecc2f155d9182e3209d97c76329b830bb62df195cb2be11223565f496e751a" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "2ff4aecc90e2de9a7d3d15eb314cc8" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212801024120_1)
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
        
            key_len = unhexify( key_str, "80ecc9587bc2cec1ba87ab431c7ed03926169c01eba19729" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "5a65f279f453572e169db33807d9b52d" );
            add_len = unhexify( add_str, "29520d9020efa1ecf514e39a286f398c7225b945608d4b57ec873ae8bfbdd40e4cbd75b9b535c9f171cd7913ed4b21e09d6bb030eaa27ca58b08131817113c852b6cbdf550d94dddfde8595e689470cf92f9c20960b936ac0692171158e54041155482f29e4acae41565d87f5641d1aac96b8cb763b7f1267ccdbce234d067d4" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "83dec0fb36463b86270656681455a0" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212801024120_2)
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
        
            key_len = unhexify( key_str, "94345293fb7733fea9c8b94be2f4fc26f8c3655f583e2b0e" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "8bad4f3f289b9f4063ba39622ba2b7ee" );
            add_len = unhexify( add_str, "7e2b6520d7a554e612d01f462606c0e6d0181bafece1daf54f4316d707483a5dcd4275a08caecc1c20f3e32872fe3e57fa62d598949f5e49ef0efd53e918617e0a140338c007025493f2e0f8dbe5fca4a57d1db6333551bbca79243a73ae8a68dafb3089998359159df916ee6ba4f928a6a173390f15f2ee6045d578dd757bb1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "da305181a12517420c6f0d71fd3ee1" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212801024112_0)
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
        
            key_len = unhexify( key_str, "a3915523031c3caa58ce02c2b1e6ee2eb42cdaf31332432c" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "d5416986beb3131afd6b7967836d243b" );
            add_len = unhexify( add_str, "ba4e883147c8f07afc08735e6e439798bec60e00ed3f5982f66d6b82a9af7580934112a9858f83abbd71193190298f0683453d3f8388c475fbbc8f9b6a3d2c77046b73986a54cc4559c57cbb86330267e04bcf5fd583c6d2968a7971da64c99d98623676154b0ee413ba531ebf12fce5e06b4ee0617e43bdaeb408b54d1b4445" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "f273fe664e5190a506da28ea8307" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212801024112_1)
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
        
            key_len = unhexify( key_str, "799d3ff266644128f330ceb8c028297991b2a5593e4afa3b" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "9d27061dd9d50817b3086f453f1f401a" );
            add_len = unhexify( add_str, "d3b5c420ac597daaac7503cd17f580e94ad779fae0d4199ada2c7da7c4a611228752375647a03241f29f810d3a6a74a140ef9651e4a6099259f7d41ec4e51a02917e8cc35edf7f60ffc473805f56f0ad51fcc767670157c050c3214d36f831a54bfeb7ab2039cb10f7919b89b0f623a572aaed313983b105fdff495d979b8a84" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "e690c9afdecea2494b6cf5a576bd" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212801024112_2)
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
        
            key_len = unhexify( key_str, "7480905cee8be7f42b9490936041a19b060331712882da55" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "27500a09506e0133c88f65e77721b547" );
            add_len = unhexify( add_str, "52832d4118fddf182b21513db25d54a19220335f8676ea35c0941d2a38a3aa536b8c9cbf093de83c6b24da3372baba2826289bb3cac415795b9bd3ea62bb9b48450978e79b936cd70cd551e580a6de3418a2be0f4c1f062954aed6adc94d805e59ff703d239fc2318b80cee45c57f916977b319a9ce884d7e02726fdb71c3287" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "52a5721e98ba1a553d8e550f137c" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212801024104_0)
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
        
            key_len = unhexify( key_str, "042db3f8af95ad68966bce9ca0297ed41b608683a37457f5" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "32d3e97edd3f393da5abc3827cae1e67" );
            add_len = unhexify( add_str, "4d7c2ee6e024e95a6e684ded9898f5c7fae7da8658bdb40560dac6495e46a691e97c047e66046b55e8cf9b02d31d3caeebe3a9f8aeed756d6b0da1ac5d4ba2c5e7b54add22f681ab1d5a2ac1463e8447e08592e0c2f32605bd02f2f03c925a2159e5bdd880323f4ce18a826a00962ce418dbbd5c276e3ff30f1cbaa4795d1ce5" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "e2afbb95a4944353ed21851f10" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212801024104_1)
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
        
            key_len = unhexify( key_str, "7f5ea90f99fc76594f0f06448321bd4bb5e494a5e351e41b" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "002a5da3c543ca56dd7e5b013b094f70" );
            add_len = unhexify( add_str, "b8150b50e36de85158a96d2096678f31f179c8765ae6ba5723ca655e890528eae96d438f9d9365575dadea3cebb4d7df3a9d5323f93696c40781a6661dd4849531e672f5cee7cdfc529416c9c3faa889d0f66ee4049c949c3c8459680f490bbb0a25e50af54de57d9e3241e0dff72604af55827b9c4d61b7d1a89f551cea2956" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "db9fd90a0be35a29f805989410" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212801024104_2)
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
        
            key_len = unhexify( key_str, "da287d34379d56f542edb02ea673bac097150f87648a57b9" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "6696034b1b362927b89ae1b7ab5297d7" );
            add_len = unhexify( add_str, "45818b7b69b05a121fe5c573c9903cb11477873b24a544ba919baec78d1565f4ad0766da58bfabfaa17ac3c628238a4d38b5c0b14b52e397bcac991408dd7b322ff614bd697ce2b5b94ca155a4442ddd9e502c4a5f81210c32dff33481f4211897db38f619b308f3708d554bdb6c7b8a4d2a80ccdfd5f70501c83502a312ca8a" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "8e65d86edc071446454a1bef34" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280102496_0)
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
        
            key_len = unhexify( key_str, "1782ac334cbffc92769a170c3cd43915f735b4423ebb4dc3" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "736f2f24cd04e26d38e69c55b38cca7a" );
            add_len = unhexify( add_str, "5827d391efec2f8f102e5f053ac496e2910248a0eb72e8a0b3bf377c6349df02ab0410a3d28bc27abc7cbe582a03000db57843565e4fb06c4078de75c3f1a21130d55befb7ecb919ad789a4de2816c3a42d4e9b32e38d980c06045987d03739cbe7710d839c42f04f5088072c1a1044c3b89809b780e74e54ec135fbe4129ee0" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "c6dc3c4ae52f3948503d84a4" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280102496_1)
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
        
            key_len = unhexify( key_str, "20529c374f21c97b0a8f96f7bd5bdeb3fcd2b12db30b3ee4" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "e6e45b7c28f7fbcae658acb533614e48" );
            add_len = unhexify( add_str, "b41290031906709ec8048f450a940eff0422a6ebc7b44666c05f17aec9debc1bfecce62d896d5df4618517fb57ce7b04ef1579ebb2636da0eead063bc74ec184b0a69ca3eba675fc7107bb52a49f93783608677565205ada7bf5a731441e44cede781120a026cc93cfe06a06fb3104a521c6987f1070823e5a880cbb3a8ecc88" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "e9ec5ad57892ce18babfde73" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280102496_2)
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
        
            key_len = unhexify( key_str, "5634789b29e373760ecb9952f4b94ca76f40dda57ba363dd" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "7cd1d2d6beef44a6d6155181dfca3dc6" );
            add_len = unhexify( add_str, "0130a67935e2df082a95d0bc6dab17093fb08746a809cc82da7893c97c5efc0065388bb85c9c2986a481cc4bbdeb6e0f62d6cd22b7785a1662c70ca92a796341e90a538fe6e072976d41f2f59787d5a23c24d95a4ca73ce92a1048f0b1c79e454fb446d16587737f7cc335124b0a8fb32205e66b93bc135ad310b35eea0f670e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "4006685e2d317a1c74ef5024" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280102464_0)
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
        
            key_len = unhexify( key_str, "f0072110572321ad9804efb5bcbc2ae7b271b1cbb0f4897b" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "604ed8056666b17fd27b111afd419375" );
            add_len = unhexify( add_str, "97f68c00513b2247bc88a331a3ffa1208038736d6761b3b080884a8dd46e0596f2c00c1a93bceeeee814210e57d7f1cbdb4e0c2ea6a0834baf716945af9aa98e2826ae0eb5717b241ede2b9e873f94c1db9eb5e1b25f75827c25849a2c7b92098b54845ed81f52871a2b0d12d317846cec34defaaafc3bd3cc53a6ab812bd250" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "64881eaf78aeaa7d" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280102464_1)
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
        
            key_len = unhexify( key_str, "e91e8c2d6928bbaf870e141ee34d3a56d00dacc8c7e50514" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "6f3d661a3e321844d1fc12d5ec2becf6" );
            add_len = unhexify( add_str, "fc8e5b45ad1647f9dbdbb6b437abecf0a8ac66065d0e250aa2ae75525455ee13adce8c59d643b96de9002d780db64f1eb9d823c6b9a4238171db26bf5d05153d1e3c839b93495084363b845fed75671ace0c009800454596674217b19832751252f051f3995776a89209c1636b4f4b28a364bccdedb78ad36876745c1a438406" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "1f4f495adfed6c1e" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280102464_2)
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
        
            key_len = unhexify( key_str, "138ff9c8c556ffe7637f7602cae428d7e20dff882d44ddc3" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "38d7005fadee55b5a0434d924d971491" );
            add_len = unhexify( add_str, "3facceb047e486998c945839ee5eddd67054bbb28308365b2909dabaed29fd5b7b34644043fa443165e07b20214710cd652fecd816d9273c700d6828d216db8f3ceaa9eed0e251585f4ee5ba4beb3c0582b8128a3ecc01f4b29cab099ba2a8931e56120802fdf6004a6c02e6dd00257a83adc95b3acb270e8000fd2126b8eb83" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "fa8aed1987868388" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280102432_0)
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
        
            key_len = unhexify( key_str, "1187a34ccb75fc06dafeca0235186c64ba929adac6cf6e49" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "9dd515d3481f21efbe43198f623b34f7" );
            add_len = unhexify( add_str, "8a1b00ea5d1f4e451cea71b3d2fc9bb03b9790a8ae8ae262b3e97ebf34911f9d865c8810b9fe779fff701c72f3639654e60898d1f57eb93381749f0e2cecb4ee342f5f34473215d5c46818338ff688637217fdfa8b7ee552db01973fdb6084c3c20b530863eeb1ce798046890994f5625df2a56042d62742097cc10d512a543a" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "83f45529" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280102432_1)
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
        
            key_len = unhexify( key_str, "4c1052610d05fb77543b6b517eb64b487ed902f9969a420f" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "90f4c93301371158271a8f46df1c86c9" );
            add_len = unhexify( add_str, "83d009a1238f8aa40e36cbddf08a5f3d96403a03f7d079359cd6d3d0c719bf79c908654882919dbc6c27db34007b6732cb344a0f4babd26b1209ce6b134a8d2318f9a38af034b265562097b63794d7efee306e97c6ac0a991b3764ecd936c87000fa58e6689e302f12c2851b1ffc950dad7a553c8c67e01a2270e1e5e9caf30a" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "30b3fd85" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921280102432_2)
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
        
            key_len = unhexify( key_str, "3dc62e54957bdd1968be71b7d205fedaa291349d69f2854f" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "b8bce0f9263688ca41c4cefb26e79453" );
            add_len = unhexify( add_str, "22b6d92d8908bbdbcd0ff35299eddaf0cfb039befa2d2d83c896f373b92091d145f1771c58d60f94d3548d0cbbeabeb796d7632b5da3c66ef75cb41a35e7d1b032ccfbddbb9423e0ee054bd56b6288bdf1b616492c85393e4134ff9c066b23f3f626eac63a5fe191ce61810379c698de62922d3bdbe30697a3e3e78190756c3d" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "67887aeb" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810240128_0)
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
        
            key_len = unhexify( key_str, "f170a6a761090355592968d67fb3514b8bfdb41cbf121341" );
            pt_len = unhexify( src_str, "a050f858c87d56dfcba3ac1ccf5ba60628b3ab1b89845682a95b7f291c80f6eb1cbced4fa21e3584e21528746231e7311ec319a0fcecc329e1a7aaed0a8548e613e51db78c86c8d0cefa15e30b745b952809f87d8a4a7bbefc76a8eb824827d4334201bda7743dc497ef5fffa2812e67f2a04e5c10bf464179c6178db932ecd3" );
            iv_len = unhexify( iv_str, "e02ef73aee414041b137dd3cae8f2765" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "c08c9bccf298c8a352cd72e9174f57dc9bf64d65191a9e97b43ce70afacfe76feb5b2695d72ea4635fa94144de02a54333a77c7d4adcde17c166b303f1d664e6edb081a85433a7496f91ce640f113935cdd4e7ad14c95247506ddc6620913b5c67422f599ca00b95d62a9371e44c5af5295bf96743d0f1228c96e95af3b4d366" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "d64d9ac91548dc1bad618509633e0c25" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810240128_1)
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
        
            key_len = unhexify( key_str, "2ce5a40618b8bb2d9fc1d87a3333a9cd4945cfa838c8e0c6" );
            pt_len = unhexify( src_str, "4ad4d103da3fb2ef8adcd1e0e823f4a857f1d6fa6273bb66574033c18ba2f760951ee0fdbe06c5cd3a0a30bd11142450f2d7e71af2fa7b9556b663fc30766508aa24e1fb2219f30ec23a6cd48b58944541d1f3e3fbf596e2ef263bddf789e7a666a68638081f0ec1a6cb3cb7feb0fddbe323b307675324ebceb25cba6d9002d4" );
            iv_len = unhexify( iv_str, "0c4b6c940d091efee98bf27fad79b04e" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "ad611dd6ebaeb5a634d4bfba9f965948ea79d16294b976b7c8bb57240c5d13e10a9fe7a5b5d39143000b4f24fc331cc4988685c8d6401593a420c12e6cbd7cded06504d6a1034f70153f7b5019084a66ce818761841cab52d5bcb2a579a4acd9df50caf582bc6da2b94d4b3b78922850993ccec560795417016e55cfab651473" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "317596eefdc011081f1dda6dae748a53" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810240128_2)
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
        
            key_len = unhexify( key_str, "f71d789a63213bbe17aa14f2956e9da2496a1efd1a63f6a5" );
            pt_len = unhexify( src_str, "f5bf20dc6a11ce5142ff34d6c4771dbee4e74790c4ccd3cb5af408a5c7bd706bac550d7ed56805f550efc7648ab501fbbc63a1070402626c5788f076ae40e6bef2b9aab9a4bd8550fe38f7cdb0fcca2657ca26f1f729074326f45ae932182905d849b1534d3effe20dbfc3fc26dc6453d6544d481e58891405dbf876d0f254e8" );
            iv_len = unhexify( iv_str, "17327996f18c7096fc5b8e375ed86f47" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "fed961a497502b2e49043ff29b9426a1e864a7fe0a88281a1572fbe62203f071710ea1d77873906369b195919a7bd5b44cbabab6eee23c3692cb8b9e4db7ee595b8d4b063d209b11d64150c45545b7eda984144e1d336a3bd3f187834bbc6950b3e7cd84895a3a5e27f8394a9aa9b657fba77181c9040b741c12fc40e849ba4b" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "9dba8faf9d12905970ba0e29bc7e9dc4" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810240120_0)
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
        
            key_len = unhexify( key_str, "83182ba753ac16554e873281599113b7620bdb042704bce8" );
            pt_len = unhexify( src_str, "6915d46189fcb0f9ab9b838da2124ce06398d638fec9c1c53f07a43fa0ea09feb2bf868fd1dd521f301f9f38e2e76716038f34cc0d18ab9bf27ac282dc349002427ca774e211027baacb9f6bfad6fd7885a665e508f654bb018f0323215153cd3a5b3e7b83482c08cf07ee5ef91d64a671b3ef22801ff21cfae95d6843ccdc16" );
            iv_len = unhexify( iv_str, "805c6b736d62f69a4c2cd4aa3745a615" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "76dcefca6305ded697be4488513cc3fd3d9f08f06a7c1a9133b9b3fb0f44badf5c7544881b5babcb873cab912cc8a00337fc36100e6a5ad998eac5d8568795b41166377c5114757044b9b73206d19fc34b6378a06d55b5d5e9498c7693e818dd962af9b9da2345f4ebf152f33fe85f3398a65ad7dec823a1b1155c38cf67df84" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "746c9972aa8481253d0d54db77398a" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810240120_1)
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
        
            key_len = unhexify( key_str, "b176e7a68da4c74aeb91760448c0257b1e17101299e1405c" );
            pt_len = unhexify( src_str, "691c436811f82e747468571f80fa8502ef5f25936fca58a8fb6b619a7a95f4938da558a3b26a2f09c8fc1f5bd347c7724d9fa377d0a52094bfaac88f3fa9b3dacc2f56d880e825809533da5980a63e01d6199fbea07f3d070e29c5d50e1013224f0ea86e7c008e3a2e63df394ef6ad93ea97d73fd4429feee495b144ef3a0d6c" );
            iv_len = unhexify( iv_str, "42e2e70b0096ebd489bfcf4d6ac0f2a4" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "81f9c34c5b0668fd58ec8822c6ba75bd7eb0d1741260fad6ad5e637903aa29d5f5facaccb4b885f62e10b7371f9b6b43e3aeb69bc5093bcccd49f3ee744e49f87cd2a2c36c808c47e4687194cbfd4faec4da66b99e3d4ced9cb8ac6ffb94d7fef3ae2b92b9f613f2bda3ca6c8efa9c6df8bec998e455f6eb48519e8f8ce4e526" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "26d0921dbb7987ef4eb428c04a583d" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810240120_2)
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
        
            key_len = unhexify( key_str, "8bab5bf1cd8f259129ce358cc56ace2bbbbaefa79727f66e" );
            pt_len = unhexify( src_str, "57385955b9909a0856bf54ad25d00779cd7d3dea78e1ae8965c4b7a568934d15ba1a7b2ab899f69fb1b864bd4d529319b51bf85a9b63de9cd51997ee4b2f015307cc42be9257e1b0a84e1c9e55a370476bff0a5325b21850f5b686a3bd4f1599f36d0772c406047b8ef29245c42ade862cb9d25b1e108db4f33a42dccf45c985" );
            iv_len = unhexify( iv_str, "ca5beea7dac2d9d24d548463977d5956" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "67deff1accc4f279ec2eb4c2a515c17886371bc4847bdaff4aa70e170775b64855a6fb0d347baf39bb53d7239b7a63ce451effc69e8d8c3e544b77c75170a68cbc45dc96ad238aabeb5ebec159f38089b08dfbbe94e1d3934a95bd70f0b799fd84a8f563d629a5bfbb4eb3d4886b04e7dba5137d9255268dac36cbb5b5c8d35a" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "f212eaad0e2616a02c1ec475c039e0" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810240112_0)
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
        
            key_len = unhexify( key_str, "bd0e0d0c7907bdb4b4e60510f73d8ab2a30700349206ce22" );
            pt_len = unhexify( src_str, "e6835a650047033a4940f72029857fae6fff2773f2aa0e4f7cb0a4abe86b6e8cb0c3e468637057f7eb20d1d30723e3c3107d0f579e31a4c3e6fa8133e1b1b51fd21a8aa80ec657c4f674c032bc4a2d3e1389cb877883317c4451ab90692337bd8aa6e9312388a0acddb508fa477cc30eb33a886e8fbced97492c9d3733cf3fc2" );
            iv_len = unhexify( iv_str, "1f183eea676c7ed2ead9a31928f4df5c" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "9f1a3017d16024dbfea4ba9df5154a6a2c794f00da070043c17f0204f06f637c8fffc760424187dce4fef044faccadefa1b1bd818522915e389d307caa481af0f1f767c38216fa048f621d46880afca5c8fc582853dec95d19d19cc943e9a1861597c99041c59e8bf8e7245f9e30b1f6607843a978d0ae7a4e0f716dabc9d9f6" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "4ceea20bf9616eb73cac15fe7e2f" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810240112_1)
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
        
            key_len = unhexify( key_str, "d59c476dcef60a45be253d5cfbb24742de9e3879bdfe6949" );
            pt_len = unhexify( src_str, "144696d85126c682f8446fcc2724fabe4b8840d46f3de6ae2ceacb2f06a1a80fed430e3a0242f4f7c308611c802c8b8e9c992b78a5cf401fe7a4671bf081f97520919f02b66e8bffd4fe3f4a69cf3d16667e7724890cc4b66c6ae487d2d987bfacec424fdc9865af4474b04cce03fffc828b2df66d99087e63f35eca52abe864" );
            iv_len = unhexify( iv_str, "9bca808f02295477f2aa7f6ac1a7bfe5" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "9d23989edd8dd9911a3f5a80de051ec7812c6ce018e683751380ff990a079f3502ec0fabfcdacf6c1fb2503094124c39ec531b5d29ee8e4e46c324fc10dbe0f31e9aa56522bcc7085ccf768425227cbab6db4127671a4cab7bc65dc1d3d9d81469493329e29a9a1cb7e5e088e84eb243493cdf1a49b16fc8d4ea2f142aa9ad23" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "d8b20d72d95a44dfb899bc6aea25" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810240112_2)
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
        
            key_len = unhexify( key_str, "2f1594e840375405a682dbc1836344be8c6b3f3199ee7fd6" );
            pt_len = unhexify( src_str, "9bc6b715c65347a383f77000b3efb33b16019d01605159e09c116ded52d20721bcdde3796b6f4dc33cb29ce1c48438e95d4db6102465440cecaa50ca33ebce470d8986663652e069079f9d92ff167b3f7ae568218fc62ff5a7be50b3b987dab4fc7979e5967bb0574de4bc51e774ba05f9780a49ac7b3ea46fdf35804e740812" );
            iv_len = unhexify( iv_str, "7f1f4a80210bcc243877fccd3e7cd42e" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "773d6901ea64d6840ded9a05a7351c0c74737ad27e7c3dbd38dedcdede94722ae67e88851ee471aefc1f80b29a7312fa2a6f178ef2c9dde729717977e85783e2e49a1fa2e847d830fac181e95fe30077b338b9ac5d2cfa22ff9348a0771054322bc717343b9a686dafda02d6354cf9b53c932da1712b9bb352b2380de3208530" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "fc3e0ca7de8fb79eb6851b7bca16" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810240104_0)
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
        
            key_len = unhexify( key_str, "88a6d441c1b7472aecf92c294f56f3c1da1702d174eff431" );
            pt_len = unhexify( src_str, "eecc12fbd00c636a7ff897c244593239d2dbca9d1f370660c9bf9759cc41dc6e95075516f8d7fc06fa91ff68701777725171c2dc0767a1953fac13008d77065cce8ee329283d3f64adb8a298aa100c42e75d62e47fbf5134a21b826fcc89ebb18707c0f4d54f6e93220484706a23a737341c601b56f6a28cc8659da56b6b51b1" );
            iv_len = unhexify( iv_str, "058a37eaee052daf7d1cd0e618f69a6c" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "0f5e889deff370810ed2911f349481dfb34e8a9623abd657a9a2dc14df43dc8917451ddeee5f967af832296b148d6a5d267be4443e54cef2e21c06da74f9a614cf29ead3ca4f267068716a9fd208aefa6a9f4a8a40deee8c9fa7da76a70fcb4e6db8abc566ccdf97688aaad1a889ac505792b5ede95c57422dfec785c5e471b0" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "5fa75148886e255a4833850d7f" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810240104_1)
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
        
            key_len = unhexify( key_str, "abb4c4f8d3c44f07d5a57acba6ccf7852030daa84d09e13a" );
            pt_len = unhexify( src_str, "24d82903e5074beb9a769f24a99b18c7b53c160a3c3ae4065335bec1c4170aa4c656bd7c87a8a13c0ffc6653c045445bf8a135d25a13b2d44a32c219adc6ea2695fb9e8c65f3c454dc0e2772f4a4ce51ff62ad34064b31b0f664f635de0c46530c966b54e8a081042309afb8cf1f337625fa27c0cc9e628c4ae402cbf57b813a" );
            iv_len = unhexify( iv_str, "c9489a51152eec2f8f1699f733dc98f5" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "3e5528ab16aed5be8d016fe07f2ff7ac4d393439c4fe0d55437a68967d685815e359fdb8f77d68241940ce7b1947c5a98f515216254ac29977cc2a591fc8e580241442d08facbdbee9a9ff7cfbde7004346772b4607dafb91c8f66f712abee557d3da675bb3130e978a1e692fa75236676b9205341ead5277cc306f05e4eaea0" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "fecca951ba45f5a7829be8421e" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810240104_2)
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
        
            key_len = unhexify( key_str, "cbce5e6d0fdcd3ab08ccd86115149b5569584dfadf40256d" );
            pt_len = unhexify( src_str, "3974339a1b90b19fd3857d812a0e59dcf43f9b0f360839940b99834ddedead79785396ab8fd0fc0e523c06f0555371fd5bc857a95c3ead26536e6deb1faabdc776ac7cfec4b60d9c24b0856ecf381efd98f941d5b2a38108922d9cf1113d1e484354b55f9c0f09d95a77fd30ec9cc04d19199931e187c56fd231f96fce5e1eb4" );
            iv_len = unhexify( iv_str, "ae3a25be73876b6e9dc88573d617653a" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "4f57be0de00ca2c7c52c54b931c235fecb4ee1e5a30e29bf68f57248bafad87e484cc68465d9f64bbf502cefd2c84e5596c3c8e58a9fb51a8c8b132579a94bc32e92f7c7247dc5f69fda98727c423de5430f01b37d77e3ae6bcd06eaf5625e5c7c9c228b9dca5aad8f571369fe0964731bf1f143f2f709c7ed51641ecfc88ebc" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "33375e21fd8df9f0196198b4b1" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024096_0)
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
        
            key_len = unhexify( key_str, "96779eaa8699469e2a3bfae8a03fd4bff7abc62d427ff985" );
            pt_len = unhexify( src_str, "a343fd32fc513e0e9772acbf99feafe9de4b54e404807999b02e921e0914b2d64d0d402ef06f31e1db852899fb6db231ad4465af015b0c16407fa3666ef5c2a6d52d5b4f60b0f7fbcb13574b2aa5183393f3a91b455a85b3ed99d619bc9c5c2dbcc4f0a61a7b03e5ab98a99cee086be408ce394203f02d6d23a1e75df44a4a20" );
            iv_len = unhexify( iv_str, "cd7dca2969872581d51b24af40f22c6f" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "74422abbde6e4ab674025735874d95d9fe3015620a8f748dbed63ef0e2271063b6c0d65e00d41bcf4ea86ac8b922b4d475f904c0724f0adebc2eef4a3abd0f9efd75408cc054cbd400436e0545e09e6b0bc83a9c7d1c1717589d180c7b1d4fe4ca18bde4d9b6bc98481b7971c7eb81c391ac4dd79cdefeabb5bbc210d914d30c" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "b0e425435fd2c8a911808ba5" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024096_1)
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
        
            key_len = unhexify( key_str, "39bfb4cf533d71c02932e1cd7b800dca9ce9bca843886962" );
            pt_len = unhexify( src_str, "de76f63ecf9c8d4643599f4dc3f9ed837924915ce4216759013cdb46daa0a508e06bcdb77437b0a58c40a0bd30a05ca41433218c6869f1ecd26318aff27999a2ebbb651de8e03061b8ffe3e14060720eb35a8e4dfd8c870aa4562291e3758cc1ea6c4b0fafcf210e10b31f8521bb0f6b29e8450b0cd6f8c8196ca2f7acb807a3" );
            iv_len = unhexify( iv_str, "d2b937bb5d2ea7d54d2b96826433f297" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "0b0b4c92f06b17103ed581fb32d46e874fea2a2171d32aac331daa4d6c863f844fbbad72e455cd5a3ef941d8cf667fed5855da6df0ccd0c61d99b2e40a0d697368138be510a2bf2e08a7648850d2410e4a179a6d0193e49a135524092ab1f842ed4057611daaeb93e7aa46e5618b354a1091a9e77fb92a8c3c0e8e017f72deb3" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "a188107e506c91484e632229" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024096_2)
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
        
            key_len = unhexify( key_str, "41b7d80ae487ac35aa498e5939a0f27baeedf48a494c8e91" );
            pt_len = unhexify( src_str, "c26d4b918a0c967141fb5712a28698d16640d200b2934187b81ec58486b706ea1caaeb654e5fbbc0d078215aceed7d66939e0fb54d6131d8948cf58ec9571646ca75a051c2b5c98fe16f7733d42e5897b0263272015042f3134143ea3b08bc65292d8d31f30f2ed9830ccbfca2d33d290c28f4dad07c7137a4ca05f432a457c2" );
            iv_len = unhexify( iv_str, "626e1d936b38cf9c4c3a44ee669936ed" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "8998e799985890d0f7e8b0fc12a8a9c63171e456ef5cb211f836a2dc7c9e3f4d1cd6280f9b0c469b703c55876b57cd1d8cef70dc745e3af8438d878cb2fe9fb1c5b2d9a2d90edf3bc5702ef3630d316574c07b5629f0db1510460af8e537dcf28d9c5b5cec6996eaa3dcde3354e39f60d5d896d8bb92718a758adb5cb9cc17d5" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "69901cbafe637de5963e7331" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024064_0)
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
        
            key_len = unhexify( key_str, "2ecce8fb50a28a085af744b44bc0ea59d6bc2c8ff1f2ff8e" );
            pt_len = unhexify( src_str, "54300bfd55b227b4758cf64d8a3f56cb49b436adb4b927afa8c4b70d2584a6cba425af4fbc3840dd6f2e313f793cbc7aca8219f171c809cf1eb9b4ae8a9d0cf1a7aa203d38d67cf7719ce2248d751e8605548118e5bb9ce364349944a2205e1b77137270b83555d5d804edba2f74400f26d2d0d28eb29d7beb91e80ad66b60be" );
            iv_len = unhexify( iv_str, "b7e43d859697efe6681e8d0c66096d50" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "45dac078c05e6a2c480543d406c23f3dda63f2b616007d08fbfb075a90eefab8dfbc26d334266f5d72fbc52800cf457f2bbc8062a895f75e86df7b8d87112386c9bad85573431ccfcef6a5e96d717fc37b08673bf4a5eecedf1a8215a8538e1ddb11d31a24cb1497c7b5ba380576acb9d641d71412a675f29d7abd750d84dfd1" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "2dfe162c577dc410" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024064_1)
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
        
            key_len = unhexify( key_str, "6773e627f6c49a1687a3a75d2ee6754ebfc2628bdfceba28" );
            pt_len = unhexify( src_str, "eb0a64ad510968c68a816550d9fe2eccab3bd8409ab5a685a8638f81b4b50a9a96318bff4e86f7f6e9076960be8eef60e72cee4ea81f3ba269d8ab4c9581a54638421520a6411a83e9dc83b6981a9dcdd9e4a367d57f156d131cf385c01a736b327218e6b6468d317ff78a01f1588c359a3a9b188bbe5d3ffad6b57483a976d0" );
            iv_len = unhexify( iv_str, "ad85becb03a05caa4533b88940ca141a" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "959658fdff5fd802fca5c5a79d59536ba8ef1359ac7bfff81264c7827bd31b8f02ecb54f309b442a54a5a57c588ace4b49463f030b325880e7e334b43ab6a2fce469907055e548caffa2fe4679edbe291377c16c7096a48aef5659ad37702aed774188cb4426c3b727878755d683ed8c163a98a05f069a0a3c22085600759170" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "4c0f4621b04b5667" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024064_2)
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
        
            key_len = unhexify( key_str, "1c086f7404c14160f33d6efde231eda610f92fa55ac147b4" );
            pt_len = unhexify( src_str, "fc8e5cd81755e489de7e3ddd2b587149ee013bffa2ce198c514641b0e1659261edd60bdbfd873e30e399869748bfe56ba543ceb9bf5fd0e7ba2b4dc175c52f28a8a02b4816f2056648e90faf654368c64f54fd50b41ea7ca199d766728980e2ebd11246c28cfc9a0a1e11cf0df7765819af23c70f920c3efb5e2663949aaa301" );
            iv_len = unhexify( iv_str, "71f154f1dc19bae34b58f3d160bb432a" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "6d60da2fd060d2aec35faf989d8df33f2413ba14842b0406e38a6a847e191eac9f4570cea647c3988faaa5505ea20f99132df2a8799cf0543e204962da1fd4f60523d7149e0dee77c16590d7e114ac5d8f88fa371dcdd254eccaa8316ee922ba23a0a07b289739413ddffc2c709c391afee9289252ddf3ddb62a4532a5515e35" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "f47bae6488f038fe" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024032_0)
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
        
            key_len = unhexify( key_str, "bae1b3eef91ba79032117c60fb847d46f18175565d0ed10c" );
            pt_len = unhexify( src_str, "9b71eeccdc91cb5f7a567a9189774f4c30d96477b88ac553df66b78a56e5c9e0986a17d80c811116d31985acfbf9d7a9bed291aa2fb6329457a836b3f8f11c16416f0a3b86dd9c717c8a050c6ceb5c27d8e2ee0dbe63f3e1e4f0aff4809e1f6f6ed64d31d494b7399cfa0dd9446321bd4256a49d0793a10a670e3f086408428e" );
            iv_len = unhexify( iv_str, "cec8b66a657e4bdf693f48ac52e60770" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "015a318acb6198189ce908ab1af28578a37a48beeed772c6ed4dceb0a3bcb092df85f653234c56a25c075c8e028d4a8d90d974fb0477834ae2de8d5df53d0d03a979450b6e7a66fdc9b11f879ea9072699837f2de7192156f8e5d9411fd83d97d31fe63ece4e4326ff50a24fc75004a5ba2bd4845b29e0794696943dff1e5d6e" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "9cf6f90a" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024032_1)
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
        
            key_len = unhexify( key_str, "7c1582240ad301f831902c66334546dd681c12308add0870" );
            pt_len = unhexify( src_str, "d4b716b49858a23aad478581cbb6dfd015ae550d76497229b5b1776e83f2ded8542675c63ca6a007a204b497ed2ef71ca125d91f386be9b4213cd352a797a5d78a1373f00916bb993de14e1a0af67524acfcc9fd71daa32e5def9a3f2dab5b3bba4d2f9f2cfc5f52768b41157fe79d95229d0611944e8308ec76425a966b21ec" );
            iv_len = unhexify( iv_str, "b6f4f3959914df413b849d559dd43055" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "79964f8775c78009bca1b218c03056b659e5382e25e43759c8adfa78aec48d70b32ffd56b230fc1ce8c21636a80a8c150e5dbb2bd3f51607d97ed097617963dc6e7653126fe40cb36a7f71051d77e4f3b768a85ee707c45d33cc67473f94c31da3e8b4c21859002331b5f7350e3e8f9806209255ceac7089176e9d6b70abd484" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "79e5a00b" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024032_2)
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
        
            key_len = unhexify( key_str, "fd55a356943824d20630b1539627ad1a9dcd8ee2cb4dbf49" );
            pt_len = unhexify( src_str, "b8d8d6dd0631f9183ca858033a31dd583d3ee3b9510fcc69d8cd412016bf854b9edcf65c2831e63d72f4cb61a99f6f4e6dab0c2ce9c5a8cdbc179ae93aaca2c8a5b848a15309be9b34e5226aa9a5908f543fdda983fec02e4073edcc3985da5222b53f8c84b9c54c78dd8b2712b59209463595c7552e28f2a45f51cb882c0354" );
            iv_len = unhexify( iv_str, "aa89a122c68e997d0326984fa5bef805" );
            add_len = unhexify( add_str, "" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "107a9ed561e6c45c375d31dea321c7b4a4b7641024d2c9eef6a103a750ba15e1acacbcae121510b4f56f19d29e6fb3e6fc06950b1daa521528f42284130a40e5a6c1b58b3b28003673511abcf59a4b9df1548a00f769d8681978b632f75e5da2cf21b499a24fbdd4f7efe053d4a1b20b240856d3ae27948e35098aa617def5bd" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "7f9c886a" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810241024128_0)
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
        
            key_len = unhexify( key_str, "4cddc8f525640fc0a0875c65b788ea75c673f84f4aacaed4" );
            pt_len = unhexify( src_str, "55e3ccb855c1fd6d33e28d308485fd85abbd8ade1299936996851d44dde063ddc37962f9f67e95df02eaf3d877516240771c469be2abf2ef6c8dcbb79eb1976f825b109f752079957a7c981faa2fcea599cc52e262b84f4c2031821619f0be6fa3c38d660e9eb3e0d5de2da6b83de9866eb3efbc6a2dff27e52587c6f79e1c26" );
            iv_len = unhexify( iv_str, "1b883a89413f62dd6d507cd70c048855" );
            add_len = unhexify( add_str, "eeaf21bc317660b0e2afb9cd5bd450ff0bfa6cfa7e49edad600f71b971347e93b9712a6e895540c665a1d8338f61b51da9e0a4a9122409824287ba4bc06bdbba10290a40b31b5eae9dfeb6471f4a0a0c15c52a2c677c4d472630d4078ecf36dc6008faa0235a688ebbe2662e46a49b1dd58cbee82f285f3cdebda1dc54673195" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "18d11513661296035f6f42d264e0b4cc7ec47f43b758c6dac95e5e3b3834362eb64447d923e107a60cd66ac359cf3a203f9070eab9fe61ae64a86606c9b50a97a19e12f731de28719fe178c9713edbb4525b221f656a340c867405c41bed3bbcb9c6da5cc6a4d37acd7a55f251a50fa15ea8f9b8955606eaa645c759ef2481e8" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "dec3edc19fd39f29e67c9e78211c71ce" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810241024128_1)
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
        
            key_len = unhexify( key_str, "3b8c31830b1139a60425f6a34387f5ca2be6f5a5074adf13" );
            pt_len = unhexify( src_str, "95f4ea90729de0f0b890fdf697948053f656bddf57e3d461e7ee1770161904bb2cbc8c2f801481bb54145af760e91c8b30cb22faa87efcc6f01e3f798af0bd460475754726514d53f419af2f2c373c76f05bf57d3fc1b763f72ba0fd2682d9d1d76f6ce8d55b56fc7ba883fad94f59d502244804bb87bd06f1217a4a6c5055b5" );
            iv_len = unhexify( iv_str, "ab5bf317ad1d6bec9cac8bc520a37b1d" );
            add_len = unhexify( add_str, "5a47d7474be6c48fa4bdbb090f4b6da494f153a4c9c8561cae4fe883000b81769b46cd65f4ce34abc3e5c6880a21d12c186974b0c933a16ba33d511e79b5f994c38e383b93eea1259d38f9fb955480792206461dd29d6d3b8ff239ea6788c8e09c15be99f094d2d5980c6c1a8efe0f97f58f7725a972111daeb87d862a90a7d0" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "1d0211d7d7bc891e4fba1ba7d47ac5a4f3b7ba49df69fcfde64bf8689b0eab379d2f5567fcff691836601b96c0a3b0ec14c03bc00e9682ef0043071507988cf1453603d2aa3dc9fa490cdb0dd263b12733adb4d68a098e1ccd27c92fe1bb82fa4a94f8a1cc045a975ac368e3224ba8f57800455cc4047901bba6bf67d6e41f94" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "23681228c722295c480397fc04c848a1" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810241024128_2)
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
        
            key_len = unhexify( key_str, "9c2386b948f59ce651888451021772287f14a92d807d88a8" );
            pt_len = unhexify( src_str, "44f00c8a7c84e8207ec15a7be0b79c88fa347e2c3d5e8d07234536d86513bc39bebfff02efb9ff27280eb37f7e8a60a426538bc1e3830bca0e76faa33b30719fab51578d15df77893bce8740f50c491b8b9f1739a695c78406b5ee4d56f80d8d564b586b0f22ffa86eca46a9d8134a9507c5b9ad82757ec51b18741abc61f23b" );
            iv_len = unhexify( iv_str, "7a1f7d0be4c7f8869432cb8b13527670" );
            add_len = unhexify( add_str, "f76ea9d6e976616689709700a9638204e616f4c1c3a54a27fb0dc852990d81dfd6787aa5a83b9be5087d3f7dfcd522044911fa4186511de1957b80338025c6c4aa72058aa3160047cf42166aa0089e2ec1ac8ea6d9f5f2c057f9f838a72319dbd7bb4948da3bc87fc2036a0e7b5e8cee7f045463152ff80a1711ef1096e75463" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "666c4d6d3f1bec49ba936eea90d864e8ecbe0ccc7b23872a4ad7596afaec628a8165a70397289a10c67d62942e1c158f1489a9de44443ac4181e74ebf2562995c9182b57bc960f4b5d3e33fb7cf7a0c32a59c716de23639de9bc430712524d74a087647e27ff1af87a2aa0cf0b58978ad8ed616b566225d3aef2ef460be7393d" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "53d926af7bbf7fba9798f895d182b09e" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810241024120_0)
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
        
            key_len = unhexify( key_str, "5852b4bbfa623e5e2f83b888f5eb6cbe06b57299e29a518c" );
            pt_len = unhexify( src_str, "8cc85e520b45a85c69cd80072642ef1500b1e0a409c435d685544a6b96d3224cc40e5fe8a21c4959b2891d4a53bbff03db9939c655e6e92222c6b44c95204827bd800c74666db64907894bc4e3043fab318aa55a011ab9397592ced73f07a06282c22d9a57dd7a37eadb02f59b879b030d0a5005226c461281ce3061bf26de56" );
            iv_len = unhexify( iv_str, "b96f4bda25857c28fdfa42bfe598f11a" );
            add_len = unhexify( add_str, "0bfdc1b16eeae85d550a97a20211216a66b496c8c19030a263f896958e4d1decc310b955523e314647edcbe3f69970cda8e07f8b81f9074434fd86b8ec5b3fa8b155377ad28050b50523d3d185e5869bc9651d97c56ec6b8047c20d671f6dc657f4cdf73fd7d3caf4b872f3fb6376eda11b80d99cf0e85c4957607a767642da6" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "b148312074ecfc8f118e3800dbd17226d55fc2c91bcbceeae2a7ca3b376f6d568dd7fcb5c0d09ce424868f1544097a0f966d354455e129096ec803a9435bbbf8f16432d30991384b88d14bcad1191b82273157d646f7a98507dc0c95c33d22e0b721c046f1c13545f4ed2df631fd2b8fc4940e10e3e66c0a4af089941a8ad94a" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "e3f548e24a189dbbfd6ae6b9ee44c2" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810241024120_1)
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
        
            key_len = unhexify( key_str, "2bd897e969ccee405ba9becf24787a1e1be17a571442c6da" );
            pt_len = unhexify( src_str, "50b8ade5e6547c350c3f43a35a3cb641459c5ef902afc706ce2fb980b275fda62e8974d1577ef65ce9fd854d88caa10295d1045ed7563e9391d60700b5d2a4a7ba5f3de7a7d1541780b95a08eb3f0996d96aac7ee838b67ee869447617684c08566647a4991e31829907ebe4b32cfa46c0433a64f864b8b9316cb0ec2578ccee" );
            iv_len = unhexify( iv_str, "fef6a08d92b5b9bdae4c368fcd0cf9e8" );
            add_len = unhexify( add_str, "fb3144ec6d93704d625aa9e95be96351c6e25bccf1eaaaf9a1d405e679efe0f2da07510ab07533295a52cdc1f5a15ef5bec9e72b199625730e1baf5c1482f362f485d74233fbf764d0b6363075cebd676920a0b315d680e899733d6da05d78765db159c4f942a31d115d53f1d89cd948bc99c03adad1eee8adcef7543f9dea39" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "e65ed5b6d0f51f8876f483f3d8ab8fed78ab6c2e1cf50693c8511e1cc9823e1030740ac33f05a5aa0d88205bb3071a087655f28eee7d0a07945d25e3dc00221a1dade4170cab9084c47b82376d5d439bed99150811843b176543f7944b1dd9684fa9a52117c2335dda750d9de0d9b3ef718123b6534cb012080f6ef8eda8d4d6" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "468546d4199b9d923a607a78fa4b40" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810241024120_2)
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
        
            key_len = unhexify( key_str, "12141d5834b8ca48b57e0892b6027c997669dac12fe60411" );
            pt_len = unhexify( src_str, "cf475b50672fd8cc4ba84d17ab1b733fee2073a584d5427155f144ddd945d4901d5a9d76e3d6ae55ab3f9514861c83bca7d53868f35bdc8606a167ac83591be30ddb954ee173ee172e8d7742a71c0fee04ccd16fb5d54a45820640405209e20f8494f08d791a2a15f5cb848df689296a04e4b01e2c19bd8d9ca8b4525853549a" );
            iv_len = unhexify( iv_str, "b6dcb39939a31df176dcec87eb8db90f" );
            add_len = unhexify( add_str, "daf4e0cd0b29343defb65562594b2b6fd3f005e6255500330f77a0550c1cfbade5f5973e836ce7046bc2b2ab8bb7983830ce6ce148d0998116183d1aed320d28adef9ffab48e0f6d6451c98eb83fafc75fb054991d123965dbddcf74a2c01c746bbbc8276b77f6732cf364d8a4a5dbf5aedbbe16793e8c406ba609c90f0e7669" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "4c2d979b9c2dc9cbbd6d4ed04094285a44df92e7ebcdee7feccf04c66c45137a7df12110b8af805f5cae9b4a225c3f8dcfd8f401e05c6ce937cbfc5620acdf3a4917c5b857bff76f3d728cf6a82a5b356fb95d144125d53e568b313cef11c11585d310ca0f7f1234090b1b62536885e9e39b969060ad3893e476e88941fe2cdd" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "99cec94a68d3e2d21e30cb25d03cd2" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810241024112_0)
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
        
            key_len = unhexify( key_str, "14b9197b7980d95b71ce1a1de6577ce769d6af4cb45f7c8f" );
            pt_len = unhexify( src_str, "03b37942f12435f1c19dbcff496738207dc92edf1ab6935b564e693da1865da67fb51e8a838559ae1640da441f22ee79787f1e909cf3c32187b41a48fbc595df1c097fb37881b329fd7b30dd1e05d6052fe81edf2e10786acc8aeeb4fac636aac9432c3be3dafb55c76ec85cc13881735609773350b95eedbdb695b2de071a03" );
            iv_len = unhexify( iv_str, "cad0cfa7924e1e5cff90d749cfadf9f8" );
            add_len = unhexify( add_str, "283c8a38c7fc9dce071d4ff9ed79002a6862f9718678b435534e43657a94178353b9ec7e5bb877db5e4f62a2ca6bd557562989363c6fdedbd7f0f3eeec5445c41a2a8bc98117a1443ad4d5dd63a07806622cca8ea6f9f6019bd511634db28651b916e2399bbd84b03f8ec696ed5846f30320adef22ae6d164aed09edcfa25027" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "83940097301e9867623c107d4447b250bf6db7d06f9e07b8d8bc6b72b079b725ea1f4b5f79bb80c518bc69a2bd73cf3aa7b88162773ac5b27a2dcccecce66e158ec0875937910e0b6f396cc7d7cac5d53b0fddf3cd70b570a647245a5264927be1b2d9c46fbc6a630b21fead46c4f35af1d163268e49a16083590893e6df4671" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "3e3f677e68208208e5315b681b73" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810241024112_1)
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
        
            key_len = unhexify( key_str, "80e2eaa70362203b7561b135db581cf32e9cd816464f0b2e" );
            pt_len = unhexify( src_str, "62cc2db32584a8d90f348be32224bfdcefd1fd25c5cb05c7e74becb4b40ea09d6495f73adc1fd23d148c11849bd825efdf15e144587f785770d2aef2788b748c338373a0ea43882141bc9f7c693a291c512cdcdea6d5defb2efa2324736df7fc4b434d7f4d423fb1b8853ec3fdf2c1c2881610a8d81da5de5e761f814ed38e35" );
            iv_len = unhexify( iv_str, "3d7e99ddea0baa45e2f9f2289d2182a3" );
            add_len = unhexify( add_str, "71663fab717ec4d9da34d4851437f4504dbd71b65b0d04eccc513282c351925c23892958b4c9dc023c5a34944ef507e0b40857d8b508ab7104d13c2fbfce2d086d466291aaa449ad36977837216a496ff375959afe4dd50dc2620a062c926b939ffdb144a656bc04bcca8d1d4fa0a9cb0a5d713721accef2d2c9688a77bb42bc" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "1c56b492f50fc362c5bf70622f817e1814ae0b69db7e3055fc9e690d2adb940f9a78cfd7e08044671913baec663d9f9af6dede42fe16d200e8421d22066009535704b05b3775ac41359d7c2697e2f4bec40df69b242392eb30e2d8a664d84cf95ec21797f1ccddb72926cfdff22848d14e373f5e6c3dd349196464c98dc38365" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "e0c1b140cd7bc4ded916aab8780e" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810241024112_2)
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
        
            key_len = unhexify( key_str, "4b7aa649cb1488a658b4387451bf59852e845ec7d2273c69" );
            pt_len = unhexify( src_str, "245251595d10d719d8d00610d391735fad377b60d7430c7db488488c1ec25c12ee0dee3aac3d7dc19aa602924a1f27a2cfa8f6354315db93b5e4d2b6e8402c4254921e683ca681dfb3c7f433a97f119e01f2acb20988dced8494e086395351f2af356b11832472cbcb109c13ff92f10a4c8fe69bd264c8933cded19a980bdbd2" );
            iv_len = unhexify( iv_str, "07b50b1aacdadeb03e7488458db03aaf" );
            add_len = unhexify( add_str, "2a7970ee97d612b63d2a0c29e5045ddfc6621c237bc270b3147fc0191de199b6923947e3bd3750de5155e1df29caf96ac702f948c38619e218138945595156cc5f1dcfde0d1d6a5aec48ff37c9ff2b2209a904c59593779820ea68ad95898c7ca0d0d81583c44feb0fec30665cc56620a8c9408e4275e60f5284ed7c0e58285d" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "6bd53e4415765f387239c6664f837371b39f6d7ff22453211e91de5dd14272784fffb4f6b2c0bb8c6b7d1cafc55133aa0d54d410ae383008fdd87645655062322fbaa06df0a2d7ccf4cc170d1f98ec6a7ad524a3e5b07761f8ae53c9c8297faa5b5621c3854643e0085410daf5bf6c7e1f92bbbfc3691eeff1c5241d2307bbc2" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "78d37215234f9a32571d0d8b1e51" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810241024104_0)
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
        
            key_len = unhexify( key_str, "512bbb490d062fe5ecc8e5ad95920a9e9b78bec6a7694dc2" );
            pt_len = unhexify( src_str, "862f2724ad82a53e0574c0a2a0515bd86c5ed0b5ae92278a78ea1a90c03059d08a91d1a46678aef862b56d0320e970b7f941b784841b4d8a38d056f2bd352d48c0028086a36426bbc1436da9e021dcac705b6e03649b426cebd7a235f6d060ab6302d777fc9316db4a85e8c1387648a8f5ce2398a247413cb9374124449e498d" );
            iv_len = unhexify( iv_str, "2d14fb3e058f97b7c9e9edd1d97cac7e" );
            add_len = unhexify( add_str, "290078e63c81abfe99010b8344ff1a03dac095e2473d7a31888102e838768892e8216439dc3355aedd073892f4449d9d4d3ea6c25a9152c329d24cc73eaa0004832691740e60f17581201c8f7f4023d8e55faa3942ad725d21dade4c03c790b5370d4cad3923527c20ca925a2ce534a652ed7e032cb1c7906aebbdc24e6b39a4" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "44e78cf3a2ce4a5e498315cb8d5e841f926408921f3665d533caebe0a7fa6c164b3d2c0b21ff3a608a7194e3194fda165ada8d5fc2e924316aa4ce201531b857877c5519f875eb49e5908d8d81b69472d03d08c785ee374c5fe91b16aee173761af7ff244571fd40aadabb360f38d301463e9da8cf8dc44d20848688ab3be47b" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "6037cb18f8478630bc9d8090e2" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810241024104_1)
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
        
            key_len = unhexify( key_str, "d3964ee03ec5e500f2f8c05313b78615420183fe2950be32" );
            pt_len = unhexify( src_str, "b9424e4a79a08a7937da1da15061c1eb9a873748691ec9c1fc76aaa164bd34873d07437d203c92c0e89c0c5befedfbb17f721f576473253617547206fb2b340945536cd7a049864d099419cf3f7a9154c0ac8d676b0e9ec02947caa4057560af347ddb46002703f3531f27b2197790ba135e3d3c0709c86f4781890deb50f3ba" );
            iv_len = unhexify( iv_str, "d3d4e5fdf6e36ac75b4d51c47ce5b8f9" );
            add_len = unhexify( add_str, "6146a97a2a1c709458bef5049088fdf339e4fe29cbdf519c93d525b71c9fb501c4b58bef49d43cc7699b18fc89cee1a4a45834f517214a77fb3b91d741977308e1585c474245802118d0e2c7003057c4a19752a143195ec2a57102cb2a127d2dbefe1168492e072e74c5f6ee102a0c371b1fe2ddfd8ecbc04c6f42befecd7d46" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "a2ae334bac969072e754c0e37765ca6253744941a35587bb4feda54233a7a59f037e971d254c67948b16e4c35f306c0984f00465399405ce701ba554419a736cdff5a1b4ae5ab05e625c91651f74aa64c96ab628243d31021ad56f535eae33a885b45730268f900b6df0aff18a433e2823ddb0628a7026b86b3835160e5121b0" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "817be7dcf7adef064161b6c42d" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_19212810241024104_2)
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
        
            key_len = unhexify( key_str, "7a8049f521fe9a00f7bf566369e540a48ab59d83305e2829" );
            pt_len = unhexify( src_str, "67243a336a10b82a0a8638b35dc147c14ac63b20977922a13de459ae2cfbdb262a79004c3a656dfbc073ec8878595e24998dc44b9435439af117c9635c479676f6edb8f522cf01571be5aa5b5bc7d1cc3264436566f8d3c684973d1e88d46282b53836a1ab5a698560e5bf7629ec12cb141867f684b369546a1d8bf48315b6c7" );
            iv_len = unhexify( iv_str, "e4d81f71e1de8cf4689bfe66a4647f15" );
            add_len = unhexify( add_str, "4cf6733482c218af832e99970d0717ac942ebace0fed4ce4dfa1f710b9e131a21cc03dd3ced25b78bccd1991a30bb53b463c1440b6543b19af91e31c18866c2acebb78c2a340b930518e61a63ff8d6a6e8e7960523de40a178614dad4ce5ab253e1090a097f8ec00dfeecb46aa0e8f772f01c4e706de7e824386a13944600542" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "cfa8ba247ada9e6b3e5ab7dd0a7108574cc811c2986cad951168559ff697b77684880ec266f0b7d87a2ff559e368a85846becee312bb2991692d928a7c191cfdb7f1468f8b84be4bb592ea640743443bd4941a8b856c57be21eb22fcb3f6c0a80728ddc9dc5fab1c77dfceb91699009054c5a4eb0714a10b74cf0e09fa630299" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "1dcee251cda10b2ea8f2bfe6a0" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024102496_0)
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
        
            key_len = unhexify( key_str, "657567a56e585c84e4033268f08f712aa280015b77cd657f" );
            pt_len = unhexify( src_str, "96d889651c4f3f5120bee233f6395fa0bbba1f6548b109be568ff96f11d24e34d67beb6c20268feba89240674b0b4552d0a6455d43e8edf943da3d8d785a5221df8ddb3a98d2fc611ac7362aef71f8f004eb455a16d1dcac488ee83d4f11c4a00c29d9990c5a2a97b897d67e51faa40999b1e510ac62fa4859123cdb37d202ae" );
            iv_len = unhexify( iv_str, "94dc757b6bdbfe925b762923cd0a08ed" );
            add_len = unhexify( add_str, "a2c54e8da7dca49c73550bd1f5e68449295f062d5dfe5aa4201bdf353a2a1ac9c3c61f2b5482184cef481fa378a1ea990ce203c2c7d76993c62b415ece06b9b7caacec0c4147c0cbf292e528d97c1a176fcb1ca6147cfa4bcce92cbdfe617738a92273282c7a65fcb997bceb867ce01ec74541582d3961dddf3a2af21cad3ce6" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "55a5d07a77fc37090c4206f19483aa3cc03815194ded71c2b2806ad9563edfebfcf962806ba829373947e3e93f4f39794514ad7b6dbc626e29fbc35f90f573da33ab6afb5c94383fd0fdd1ee074d650d192f6d08fbd1e24a6966a81a2ffd83fab644ee914952de77e9427262314ac47c11a44bf7d2890f9b9980499bb6a1f692" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "41c72043f6116ee6f7c11986" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024102496_1)
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
        
            key_len = unhexify( key_str, "61159242d48c2ca0c30377ec2ad701135adb62d113c9f9ba" );
            pt_len = unhexify( src_str, "8ae40603f6cdae4b63ac7b18b4bcbb83c65867c2ae270102efb6f00aa8af5d0400dc95085910a50a16cbcf71f06c3f3eab71345d59c6054aaac02971111c7146add8c072158e0b374d481bb540036a136ccb91523f96f24ea237940ab011ad38f2a3095c0785df91604be1fe7734cc4119b27aa784875d0a251c678900334a0b" );
            iv_len = unhexify( iv_str, "4fda7236bd6ebe0b316feeea31cb5ebc" );
            add_len = unhexify( add_str, "ed28e9954634ec2c9e2df493062abf3ea3e199299053a15ce8d6fe051d1076287e4e7c0b2bab0a599b763a29d0aab680626f280c4f5ad94b7792d9af532681f6e4eb2672781f2342304daff902d03b396853eaf585af4d3bf5078d064e9eea6e94e667722f15c004f4cf52253a5c65b75319b07ba539558d8a2b552390a21577" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "dba251e35422f60f902f594bb58dce37131e8ae06b5f40ad23c4a70a5e25fe24c76982c9bc11a7f4e3cc62d8c1326170432633eba1634972a9bcd093b08e1c63ece07c4be79cadc888b0408e40c09636e1cf1e5e9a6f2ea44eea5409a2ffe9c3ac9a18ad7aa9041f08eb109c01ed90732a8afe0694319ef98a0269685b4d16b1" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "b0feebfc8324fd1e9e40f7f0" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024102496_2)
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
        
            key_len = unhexify( key_str, "5b4c37150f8bf0e14e0bfd37ac14e606dd273577007f24b4" );
            pt_len = unhexify( src_str, "48c6486b2691b86f5f107e8fe0122a821248206d2dd3ce898a2bb3772202ffe97292852bc61513529ad95faf6383b5f6c5a7c16c4cbe33cb02e5e50f32db95ee2962aae1c9c0f5470b3baa216cc19be5ab86b53316beef14397effb8afba5b5159074e26bf5dd3b700f4ea5abd43e93ca18494e1779b8c48fcd51f46664dd262" );
            iv_len = unhexify( iv_str, "664f553a14dcd4dcba42f06e10b186aa" );
            add_len = unhexify( add_str, "4386e28ebd16d8276c6e84e1d7a3d9f1283e12cb177478ab46acb256b71df5a2da868134ed72ef43f73e8226df1f34e350b7f936bd43caff84a317b1e5b2e9a2b92ccab1e3e817f93222dd1e2cf870d45a8458e57948a649360c6e2439bbcc682383b50bcd3d8b000592c3ca599e598a03b9953af485f1ecc22501dcacb7110e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "05fdbb5ad403d64011e15d27cd6f5a2247e018e479e58ad3fee1e0e8ddd9e114c0e82f2c947ff9af525ce752f4aea959463899542b85c9b413d065ea175103c3b3c35f56eea52af2c54ec08a1d5b7cd5ee4f59de8be86512b770e42ab176b6b70ccbcd264d6d5cfdd2e52e618dc24251ac339ea38cdc446c778d2db3c7c3e93d" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "77f32401db21adb775e7f1d0" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024102464_0)
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
        
            key_len = unhexify( key_str, "531a380b109098eafd997bd25bfde4868d2a1ca781795e9a" );
            pt_len = unhexify( src_str, "466237db78d4c770a658b9693420a2e087c978fcc434c9ac82f3e2447b2fa08be32d2ce6da25846555ffe5764234b07b35dd1d1bcb710e8a49f918f2c873681f32765b092a836e9418faba61dc59a254c923159be16f585e526616fedd3acfe2748ce19ee03868ea9836bee2c6acb1b821e231eb2d30d300387c93390d51e3a5" );
            iv_len = unhexify( iv_str, "ad079d0b958f09732aaa2158f6215573" );
            add_len = unhexify( add_str, "09e002c2c48beaf1122411e8624522a9e90cc3f2a040c52ffcb91136519277c39fd6a79292b8835e0fbcaef2279218106aaf75036590f8a46f6b6912053a3b391849f7e204f096288d6141d5f80c7f91dd2f2b6ebc1ced6af8216e0a594814b56bd592df800299b29e26ed7461ba3f6f3cf151b9c10ad634a01d9c5e578aa372" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "d1f49f94e6fbef7e21abad23e16c06fcdfa75a8c342be67baea8e0e57dbcd2971276e993faa124ac81e6be18f68af303518efd926513cee9dbcc5ef6cf5e9c068a1210e53fdd56776148d51597e359dbaa0570b4fe15476ccc9aa79f7c765755b6f694af4269b9e18fc62a0d47708bca67dcf080e200718c22bac256f641e7a2" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "01ec395c99a17db6" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024102464_1)
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
        
            key_len = unhexify( key_str, "fbd7a92120ff973ec69b6a8189c6ea827ca20743a8781518" );
            pt_len = unhexify( src_str, "1583c1578a8c8d272a970f05d875f199e497c55f03f10f7bc934fee21c30379dad3c580b3f99304a5747b61fd43428506439ede2c57f5229e13da9cb7cd6174cccbb397e98fb90455ccf3ea3b1304f432a070a2eb5205ed863326b3b86d4eb7f54ee2ffcd50ed6ef01b3ee216c53f4f2659a88fb6343396b2ded0b389c6266c5" );
            iv_len = unhexify( iv_str, "57658c71b2c45f6ae2d1b6775a9731cf" );
            add_len = unhexify( add_str, "45ca8a168ecca7a42847b779ef152766b902192db621d2770b56c7d592207afaf52d19a6059feb76e96b90628995bd6517af3f114e97af8d602a493b77405e93095fee6761877dc292fab696a4303102dece60951cca20cacb171abdcfd0ef6da6c90b44edba63b9b6087d876b3fff24dea909899ebd0d0371c424f51a9a84b8" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "58a290cf0e774293d1b55f5ef8a305f68605c0c81668b8a1ba95fceeaa65229404e18fa54dd811a6af085c98b8854d0f956adc2aaad742cafa9ed53d7cb445451ee7a4dc1e8399ec7e5b4d004ecd22496565bf444b2e3d82ddf6a6d5e6256c5095a699d7ff3f8cf2addec73e21013ee6f3dfc0a3abf316ea5ee1d6943bc394e1" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "af737ec3512da2b4" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024102464_2)
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
        
            key_len = unhexify( key_str, "54bfc8379e0a8180b931c5188c95ab3ed3461d6e9004d182" );
            pt_len = unhexify( src_str, "93327664eb576bbb64e4ff061874346b4e80a779cdeb1fbe630bf5e4307d4f2c5d5ecc94aa8bdea755c1af165fc8925bfcdf128c1ee6571e9f8344b22dfc90ed893316031661a9438b305396f3a80452c9b11924163b7fc4422b00dc58ee0e674710239975a2cf3253bf2601cd155e09547a5f3be1adda84a4b29631a8e13161" );
            iv_len = unhexify( iv_str, "9d15df8de4150f44d342f2031de3611c" );
            add_len = unhexify( add_str, "63331936d2972abd44c1c9f62e42bfa932dff8cc75d9f555f5a7847d08558e76f5393e08909760edbef8d2922a7ca8e1c0c505ca627c02af73253791bb35ff080b4db7dddf4c8b304999ff645227cd79f13ac87f9c963b93a79a0e946e5781cdbf1b4b1967a75314f19c7219e3b69dc2c24ba09fbbdf7184278f82818bdd0958" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "18ff87dccbc24c396190c7b37c4a77f86e609db7fb2b326802714d0f196b00b84af887f1b3bd30ee0b0b192d0801ac4e59ac40e5c652b3da32aa024da3acf648da0253674c391d260c0674853c7821861059772c9a7f2775a7ef77d1d31a6ec1c51c5f3089bb516f8cf52d5a15724281086abd92a74d255b7cc84b5051be4e5b" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "bf0f7f8084e79da5" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024102432_0)
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
        
            key_len = unhexify( key_str, "21b775ef8c40a5387d6c8eda4e90d0a00c795681a2887dfc" );
            pt_len = unhexify( src_str, "6346f84301d6d83e1c5bad44fa7e0821f35723713ee8d4a9e2bf15abf953425b09bd77b2360f4e62e82bf9e14e2b56be51d032aa8a96e894f19f3e84630f9eae831b329f7638b09de7210cd29778059ef1d0bc039c1e10405f3ae5e4ca33216adcfc21869d9f825344d62b50bab03f7aa7b92fdb94951a68acd01f1dee75e428" );
            iv_len = unhexify( iv_str, "9763e6187d4b96b1801d1f6efe7e80a5" );
            add_len = unhexify( add_str, "3bd523c16a0022b780ae8318a28f001502120bb26e2f65f4fe94019686f9d1df330e70cef1b2ba4b6ce1f7ef37750f47e602843cbc5f13ff2ceadc5091eb3601604b70bd4acad3d61950b9dd2cbfd83a391223c8e09fddd4020c0f8a8a7057139fd92f3bbe034f03cc48afdde064c8b13ea942ec0d621db959ec9d5fa95afe45" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "f25408848bc27ab087b3ea053762837a534c3702dd8be01d79f075f61d76ac1d6557d392e1fab475cc7d13a5f6be6f0718bad71c3c85b5996bd3c0159e264930988e3ed506bcc94fabecfb58caaf56e2e4315bb50817cba765636d1faa91147b3880815eeb90d0934180e49132833abfa6279247d9dd4048dff851e9a551ee1c" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "d1fb9aed" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024102432_1)
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
        
            key_len = unhexify( key_str, "8a7d8197d9ceebd8e3f6b3bfb74877ccf649ac91d7057af5" );
            pt_len = unhexify( src_str, "37b01df357561f5aa43b5b4b0081148213f7b74babc80f4b3c6dd78ad17687f11443cd4a57f8d7a74ca3080e2a229f78d8e6db276c1142d5f4ee764eaf09cfd70c596d7a2cad5360c2de20d5e17ec6e06a9b049bb10f8742a30a94270cc6d7709b2f09f3cb8347e41117b7ddb99e4a939f3094c016330a8f170ccccb9d3651fb" );
            iv_len = unhexify( iv_str, "db5144951a9f1721397b7321713a723e" );
            add_len = unhexify( add_str, "ad72fa5a05adc40fb38245da019cbf50958ccfe26abf67dfdd49f4c4af6bda8bfc99d557913b2634c5c65d33ca909360adf598b703db1dbcc29481b17ca42fce3315ea1454693b5843e751fafd78158fc040c1cbe607063ba9c0ac02ae4b88989e3cc63adda8427032c70560349e1a8ec847906a9a7b0422a694a1f9eb2b3b72" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "6985ec525cfe869e1709751eb6f1ff0aabcb39ae3aa708adc452ce1a8cad8ab4f1739f660b2841566f1f5c9e15e846de7f86ca1dc085188fcaa4a3f839ab2a5f0cfd36e36965ae519fe14f98899ccb07a3ca15ec705e3160df6dbc37ab89c882012eefe51e4da8d6d6b84b3144ca87a90864ff5390abfb92992e44c46807b3c8" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "c51604f5" ) == 0 );
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1921281024102432_2)
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
        
            key_len = unhexify( key_str, "713358e746dd84ab27b8adb3b17ea59cd75fa6cb0c13d1a8" );
            pt_len = unhexify( src_str, "35b8b655efdf2d09f5ed0233c9eeb0b6f85e513834848cd594dba3c6e64f78e7af4a7a6d53bba7b43764334d6373360ae3b73b1e765978dffa7dbd805fda7825b8e317e8d3f1314aa97f877be815439c5da845028d1686283735aefac79cdb9e02ec3590091cb507089b9174cd9a6111f446feead91f19b80fd222fc6299fd1c" );
            iv_len = unhexify( iv_str, "26ed909f5851961dd57fa950b437e17c" );
            add_len = unhexify( add_str, "c9469ad408764cb7d417f800d3d84f03080cee9bbd53f652763accde5fba13a53a12d990094d587345da2cdc99357b9afd63945ca07b760a2c2d4948dbadb1312670ccde87655a6a68edb5982d2fcf733bb4101d38cdb1a4942a5d410f4c45f5ddf00889bc1fe5ec69b40ae8aaee60ee97bea096eeef0ea71736efdb0d8a5ec9" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                fct_chk( gcm_crypt_and_tag( &ctx, GCM_ENCRYPT, pt_len, iv_str, iv_len, add_str, add_len, src_str, output, tag_len, tag_output ) == 0 );
                hexify( dst_str, output, pt_len );
                hexify( tag_str, tag_output, tag_len );
        
                fct_chk( strcmp( (char *) dst_str, "cc3f9983e1d673ec2c86ae4c1e1b04e30f9f395f67c36838e15ce825b05d37e9cd40041470224da345aa2da5dfb3e0c561dd05ba7984a1332541d58e8f9160e7e8457e717bab203de3161a72b7aedfa53616b16ca77fd28d566fbf7431be559caa1a129b2f29b9c5bbf3eaba594d6650c62907eb28e176f27c3be7a3aa24cef6" ) == 0 );
                fct_chk( strcmp( (char *) tag_str, "5be7611b" ) == 0 );
            }
        }
        FCT_TEST_END();

    }
    FCT_SUITE_END();

#endif /* POLARSSL_GCM_C */

}
FCT_END();

