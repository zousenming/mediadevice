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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "806766a4d2b6507cc4113bc0e46eebe120eacd948c24dc7f" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "4f801c772395c4519ec830980c8ca5a4" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "8fa16452b132bebc6aa521e92cb3b0ea" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "0c2abdcd2e4ae4137509761a38e6ca436b99c21b141f28f5" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "335ca01a07081fea4e605eb5f23a778e" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "d7f475dfcb92a75bc8521c12bb2e8b86" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "eef490a0c2ecb32472e1654184340cc7433c34da981c062d" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "d9172c3344d37ff93d2dcb2170ea5d01" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "017fef05260a496654896d4703db3888" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "fe0c3490f1f0dba23cf5c64e6e1740d06f85e0afec6772f3" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "f47e915163fa3df7f6c15b9d69f53907" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "14e1a057a2e7ffbd2208e9c25dbba1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "4356b3b1f308df3573509945afe5268984f9d953f01096de" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "a35b397b34a14a8e24d05a37be4d1822" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "e045ecba220d22c80826b77a21b013" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "e2898937cc575c8bb7444413884deafe8eaf326be8849e42" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "169a449ccb3eb29805b15304d603b132" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "3a807251f3d6242849a69972b14f6d" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "75683c7df0442e10b5368fcd6bb481f0bff8d95aae90487e" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "538641f7d1cc5c68715971cee607da73" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "07d68fffe417adc3397706d73b95" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "0724ee1f317997ce77bb659446fcb5a557490f40597341c7" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "0d8eb78032d83c676820b2ef5ccc2cc8" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "7da181563b26c7aefeb29e71cc69" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "be2f0f4ae4ab851b258ec5602628df261b6a69e309ff9043" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "646a91d83ae72b9b9e9fce64135cbf73" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "169e717e2bae42e3eb61d0a1a29b" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "583c328daecd18c2ac5c83a0c263de194a4c73aa4700fe76" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "55e10d5e9b438b02505d30f211b16fea" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "95c0a4ea9e80f91a4acce500f7" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "b40857e7e6f26050f1e9a6cbe05e15a0ba07c2055634ad47" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "e25ef162a4295d7d24de75a673172346" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "89ea4d1f34edb716b322ea7f6f" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "627008956e31fea497fb120b438a2a043c23b1b38dc6bc10" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "08ea464baac54469b0498419d83820e6" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "ab064a8d380fe2cda38e61f9e1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "8c386d67d7c2bfd46b8571d8685b35741e87a3ed4a46c9db" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "766996fb67ace9e6a22d7f802455d4ef" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "9a641be173dc3557ea015372" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "711bc5aa6b94fa3287fad0167ac1a9ef5e8e01c16a79e95a" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "75cdb8b83017f3dc5ac8733016ab47c7" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "81e3a5580234d8e0b2204bc3" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "c74620828402e0bdf3f7a5353668505dc1550a31debce59a" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "cfbefe265583ab3a2285e8080141ba48" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "355a43bcebbe7f72b6cd27ea" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "1eb53aa548b41bfdc85c657ebdebdae0c7e525a6432bc012" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "37ffc64d4b2d9c82dd17d1ad3076d82b" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "34b8e037084b3f2d" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "50d077575f6db91024a8e564db83324539e9b7add7bb98e4" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "118d0283294d4084127cce4b0cd5b5fa" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "507a361d8ac59882" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "d9ddca0807305025d61919ed7893d7d5c5a3c9f012f4842f" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "b78d518b6c41a9e031a00b10fb178327" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "f401d546c8b739ff" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "6ed8d8afde4dc3872cbc274d7c47b719205518496dd7951d" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "14eb280288740d464e3b8f296c642daa" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "39e64d7a" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "80aace5ab74f261bc09ac6f66898f69e7f348f805d52404d" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "f54bf4aac8fb631c8b6ff5e96465fae6" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "1ec1c1a1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "23b76efd0dbc8d501885ab7d43a7dacde91edd9cde1e1048" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "75532d15e582e6c477b411e727d4171e" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "76a0e017" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "94c50453dd3ef7f7ea763ae13fa34debb9c1198abbf32326" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "1afe962bc46e36099165552ddb329ac6" );
            add_len = unhexify( add_str, "b2920dd9b0325a87e8edda8db560bfe287e44df79cf61edba3b2c95e34629638ecb86584f05a303603065e63323523f6ccc5b605679d1722cde5561f89d268d5f8db8e6bdffda4839c4a04982e8314da78e89f8f8ad9c0fee86332906bf78d2f20afcaabdc282008c6d09df2bfe9be2c9027bb49268b8be8936be39fa8b1ae03" );
            unhexify( tag_str, "51e1f19a7dea5cfe9b9ca9d09096c3e7" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "c6a98102af3d875bcdebe594661d3a6b376970c02b11d019" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "bea8cd85a28a2c05bf7406b8eef1efcc" );
            add_len = unhexify( add_str, "f2f80e2c042092cc7240b598ab30fad055bce85408aa0f8cefaf8a7204f0e2acb87c78f46a5867b1f1c19461cbf5ed5d2ca21c96a63fb1f42f10f394952e63520795c56df77d6a04cb5ad006ee865a47dc2349a814a630b3d4c4e0fd149f51e8fa846656ea569fd29a1ebafc061446eb80ec182f833f1f6d9083545abf52fa4c" );
            unhexify( tag_str, "04b80f25ae9d07f5fd8220263ac3f2f7" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "ec3cc45a22fdc7cc79ed658d9e9dbc138dcc7d6e795cba1a" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "b10d9c70205e142704f9d1f74caee0f6" );
            add_len = unhexify( add_str, "714994017c169c574aaff2f8bad15f8fa6a385117f5405f74846eca873ca4a8f4876adf704f2fcaff2dfa75c17afefd08a4707292debc6d9fafda6244ca509bc52b0c6b70f09b14c0d7c667583c091d4064e241ba1f82dd43dc3ea4b8922be65faf5583f6b21ff5b22d3632eb4a426675648250e4b3e37c688d6129b954ef6a8" );
            unhexify( tag_str, "d22407fd3ae1921d1b380461d2e60210" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "5a32ebc7a2338038ced36d2b85cbc6c45cca9845a7c5aa99" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "9afe0882e418c9af205eeb90e131d212" );
            add_len = unhexify( add_str, "61ff8a8bc22803f17e8e9f01aff865bc7d3083ff413ce392a989e46ebed5114894de906f7d36439024d8f2e69cc815ac043fff2f75169f6c9aa9761ff32d10a1353213ac756cb84bd3613f8261ef390e1d00c3a8fb82764b0cda4e0049219e87d2e92c38f78ffac242391f838a248f608bb2b56b31bbb453d1098e99d079ea1b" );
            unhexify( tag_str, "fcbb932ddb0128df78a71971c52838" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "9bf22885e7f13bcc63bb0a2ca90c20e5c86001f05edf85d8" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "99dec21f4781284722b5074ea567c171" );
            add_len = unhexify( add_str, "9f4176dacf26e27aa0e669cd4d44bca41f83468c70b54c745a601408a214bf876941ae2ae4d26929113f5de2e7d15a7bb656541292137bf2129fdc31f06f070e3cfaf0a7b30d93d8d3c76a981d75cd0ffa0bcacb34597d5be1a055c35eefeddc07ee098603e48ad88eb7a2ec19c1aefc5c7be9a237797397aa27590d5261f67a" );
            unhexify( tag_str, "18fd1feec5e3bbf0985312dd6100d1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "cfd75a9d3788d965895553ab5fb7a8ff0aa383b7594850a6" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "a6df69e5f77f4d99d5318c45c87451b2" );
            add_len = unhexify( add_str, "041aeb2fa0f7df027cd7709a992e041179d499f5dbccd389035bf7e514a38b5f8368379d2d7b5015d4fa6fadfd7c75abd2d855f5ea4220315fad2c2d435d910253bf76f252a21c57fe74f7247dac32f4276d793d30d48dd61d0e14a4b7f07a56c94d3799d04324dfb2b27a22a5077e280422d4f014f253d138e74c9ac3428a7b" );
            unhexify( tag_str, "fd78b9956e4e4522605db410f97e84" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "b0b21ae138485591c6bef7b3d5a0aa0e9762c30a50e4bba2" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "56dc980e1cba1bc2e3b4a0733d7897ca" );
            add_len = unhexify( add_str, "a38458e5cc71f22f6f5880dc018c5777c0e6c8a1301e7d0300c02c976423c2b65f522db4a90401035346d855c892cbf27092c81b969e99cb2b6198e450a95c547bb0145652c9720aaf72a975e4cb5124b483a42f84b5cd022367802c5f167a7dfc885c1f983bb4525a88c8257df3067b6d36d2dbf6323df80c3eaeffc2d176a5" );
            unhexify( tag_str, "b11f5c0e8cb6fea1a170c9342437" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "8775665aba345b1c3e626128b5afa3d0da8f4d36b8cf1ca6" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "cd17f761670e1f104f8ea4fb0cec7166" );
            add_len = unhexify( add_str, "2ee08a51ceaca1dbbb3ee09b72f57427fd34bd95da5b4c0933cbb0fc2f7270cffd3476aa05deeb892a7e6a8a3407e61f8631d1a00e47d46efb918393ee5099df7d65c12ab8c9640bfcb3a6cce00c3243d0b3f316f0822cfeae05ee67b419393cc81846b60c42aeb5c53f0ede1280dc36aa8ef59addd10668dd61557ce760c544" );
            unhexify( tag_str, "6cdf60e62c91a6a944fa80da1854" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "cc9922299b47725952f06272168b728218d2443028d81597" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "9b2f1a40717afcdbb6a95d6e335c9e4d" );
            add_len = unhexify( add_str, "bcfca8420bc7b9df0290d8c1bcf4e3e66d3a4be1c947af82dd541336e44e2c4fa7c6b456980b174948de30b694232b03f8eb990f849b5f57762886b449671e4f0b5e7a173f12910393bdf5c162163584c774ad3bba39794767a4cc45f4a582d307503960454631cdf551e528a863f2e014b1fca4955a78bd545dec831e4d71c7" );
            unhexify( tag_str, "dd515e5a8b41ecc441443a749b31" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "5a27d718f21c5cbdc52a745b931bc77bd1afa8b1231f8815" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "59661051912fba45023aef4e6f9380a5" );
            add_len = unhexify( add_str, "2b7ce5cea81300ed23501493310f1316581ef8a50e37eaadd4bb5f527add6deb09e7dcc67652e44ac889b48726d8c0ae80e2b3a89dd34232eb1da32f7f4fcd5bf8e920d286db8604f23ab06eab3e6f99beb55fe3725107e9d67a491cdada1580717bbf64c28799c9ab67922da9194747f32fd84197070a86838d1c9ebae379b7" );
            unhexify( tag_str, "f33e8f42b58f45a0456f83a13e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "b83e933cf54ac58f8c7e5ed18e4ed2213059158ed9cb2c30" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "8710af55dd79da45a4b24f6e972bc60a" );
            add_len = unhexify( add_str, "b7a428bc68696cee06f2f8b43f63b47914e29f04a4a40c0eec6193a9a24bbe012d68bea5573382dd579beeb0565b0e0334cce6724997138b198fce8325f07069d6890ac4c052e127aa6e70a6248e6536d1d3c6ac60d8cd14d9a45200f6540305f882df5fca2cac48278f94fe502b5abe2992fa2719b0ce98b7ef1b5582e0151c" );
            unhexify( tag_str, "380128ad7f35be87a17c9590fa" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "d2f85f92092385f15da43a086cff64c7448b4ee5a83ed72e" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "9026dfd09e4553cd51c4c13ce70830de" );
            add_len = unhexify( add_str, "3c8de64c14df73c1b470a9d8aa693af96e487d548d03a92ce59c0baec8576129945c722586a66f03deb5029cbda029fb22d355952c3dadfdede20b63f4221f27c8e5d710e2b335c2d9a9b7ca899597a03c41ee6508e40a6d74814441ac3acb64a20f48a61e8a18f4bbcbd3e7e59bb3cd2be405afd6ac80d47ce6496c4b9b294c" );
            unhexify( tag_str, "e9e5beea7d39c9250347a2a33d" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "de7df44ce007c99f7baad6a6955195f14e60999ed9818707" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "4d209e414965fe99636c1c6493bba3a3" );
            add_len = unhexify( add_str, "da3bc6bdd414a1e07e00981cf9199371192a1fb2eaae20f7091e5fe5368e26d61b981f7f1d29f1a9085ad2789d101155a980de98d961c093941502268adb70537ad9783e6c7d5157c939f59b8ad474c3d7fc1fcc91165cdf8dd9d6ec70d6400086d564b68ebead0d03ebd3aa66ded555692b8de0baf43bc0ddef42e3a9eb34ab" );
            unhexify( tag_str, "24483a57c20826a709b7d10a" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "1dfa5ff20046c775b5e768c2bd9775066ae766345b7befc3" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "2d49409b869b8b9fc5b67767979ca8cd" );
            add_len = unhexify( add_str, "e35d34478b228bc903ea2423697e603cc077967d7cfb062e95bc11d89fbe0a1f1d4569f89b2a7047300c1f5131d91564ec9bce014d18ba605a1c1e4e15e3e5c18413b8b59cbb25ab8f088885225de1235c16c7d9a8d06a23cb0b38fd1d5c6c19617fe08fd6bf01c965ed593149a1c6295435e98463e4f03a511d1a7e82c11f01" );
            unhexify( tag_str, "23012503febbf26dc2d872dc" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "2df3ee3a6484c48fdd0d37bab443228c7d873c984529dfb4" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "dc6aeb41415c115d66443fbd7acdfc8f" );
            add_len = unhexify( add_str, "eafc6007fafb461d3b151bdff459e56dd09b7b48b93ea730c85e5424f762b4a9080de44497a7c56dd7855628ffc61c7b4faeb7d6f413d464fe5ec6401f3028427ae3e62db3ff39cd0f5333a664d3505ff42caa8899b96a92ec01934d4b59556feb9055e8dfb81f55e60135345bfce3e4199bfcdb3ce42523e7d24be2a04cdb67" );
            unhexify( tag_str, "e8e80bf6e5c4a55e7964f455" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "ce0787f65e6c24a1c444c35dcd38195197530aa20f1f6f3b" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "55300431b1eaac0375681d7821e1eb7a" );
            add_len = unhexify( add_str, "84a699a34a1e597061ef95e8ec3c21b592e9236ddb98c68d7e05f1e709937b48ec34a4b88d99708d133a2cc33f5cf6819d5e7b82888e49faa5d54147d36c9e486630aa68fef88d55537119db1d57df0402f56e219f7ece7b4bb5f996dbe1c664a75174c880a00b0f2a56e35d17b69c550921961505afabf4bfd66cf04dc596d1" );
            unhexify( tag_str, "74264163131d16ac" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "3a15541b5857a668dc9899b2e198d2416e83bac13282ca46" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "89bf8ab0cea6f59616eeb9b314d7c333" );
            add_len = unhexify( add_str, "4d2843f34f9ea13a1ac521479457005178bcf8b2ebeaeb09097ea4471da9f6cc60a532bcda1c18cab822af541de3b87de606999e994ace3951f58a02de0d6620c9ae04549326da449a3e90364a17b90b6b17debc0f454bb0e7e98aef56a1caccf8c91614d1616db30fc8223dbcd8e77bf55d8253efe034fd66f7191e0303c52f" );
            unhexify( tag_str, "8f4877806daff10e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "b61cdfd19c136ee2acbe09b7993a4683a713427518f8e559" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "4066118061c904ed1e866d4f31d11234" );
            add_len = unhexify( add_str, "153c075ecdd184fd8a0fca25cae8f720201361ef84f3c638b148ca32c51d091a0e394236d0b51c1d2ee601914120c56dfea1289af470dbc9ef462ec5f974e455e6a83e215a2c8e27c0c5b5b45b662b7f58635a29866e8f76ab41ee628c12a24ab4d5f7954665c3e4a3a346739f20393fc5700ec79d2e3c2722c3fb3c77305337" );
            unhexify( tag_str, "4eff7227b42f9a7d" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "ce175a7df7e429fcc233540e6b8524323e91f40f592ba144" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c34484b4857b93e309df8e1a0e1ec9a3" );
            add_len = unhexify( add_str, "ce8d8775f047b543a6cc0d9ef9bc0db5ac5d610dc3ff6e12e0ad7cd3a399ebb762331e3c1101a189b3433a7ff4cd880a0639d2581b71e398dd982f55a11bf0f4e6ee95bacd897e8ec34649e1c256ee6ccecb33e36c76927cc5124bc2962713ad44cbd435ae3c1143796d3037fa1d659e5dad7ebf3c8cbdb5b619113d7ce8c483" );
            unhexify( tag_str, "ff355f10" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "5f659ed236ba60494e9bf1ee2cb40edcf3f25a2bac2e5bc5" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "ad49f12f202320255406c2f40e55b034" );
            add_len = unhexify( add_str, "6da62892f436dfe9790e72d26f4858ca156d1d655c9cc4336fcf282b0f3f0b201e47f799c3019109af89ef5fd48a4811980930e82cd95f86b1995d977c847bbb06ecdcc98b1aae100b23c9c2f0dcf317a1fb36f14e90e396e6c0c594bcc0dc5f3ebf86ce7ecd4b06d1c43202734d53f55751a6e6bbda982104102af240def4eb" );
            unhexify( tag_str, "cb4d8c1d" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "a73f318b1e298ba4ac0ab2aed74f73543b1017cccbd1b240" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "abe33b7e8d88bd30deb96d1e90c4e951" );
            add_len = unhexify( add_str, "6de616b000047b14b6759015183dd753c61499c0e665d06a89e4fb0cd0dd3064ff8651582e901ef5d0cdf3344c29c70c3aabc2aaf83cb3f284c6fe4104906d389b027e7d9ca60d010f06ef8cd9e55db2483d06552ddbe3fc43b24c55085cd998eae3edec36673445bf626e933c15b6af08ea21cbace4720b0b68fe1a374877d5" );
            unhexify( tag_str, "4a28ec97" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "73d5be74615bc5b627eedfb95746fb5f17cbf25b500a597f" );
            pt_len = unhexify( src_str, "fc40993eb8559e6b127315c03103ce31b70fc0e07a766d9eecf2e4e8d973faa4afd3053c9ebef0282c9e3d2289d21b6c339748273fa1edf6d6ef5c8f1e1e9301b250297092d9ac4f4843125ea7299d5370f7f49c258eac2a58cc9df14c162604ba0801728994dc82cb625981130c3ca8cdb3391658d4e034691e62ece0a6e407" );
            iv_len = unhexify( iv_str, "eb16ed8de81efde2915a901f557fba95" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "804056dca9f102c4a13a930c81d77eca" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "a249135c9f2f5a8b1af66442a4d4e101771a918ef8acee05" );
            pt_len = unhexify( src_str, "c62b39b937edbdc9b644321d5d284e62eaa4154010c7a3208c1ef4706fba90223da04b2f686a28b975eff17386598ba77e212855692f384782c1f3c00be011e466e145f6f8b65c458e41409e01a019b290773992e19334ffaca544e28fc9044a5e86bcd2fa5ad2e76f2be3f014d8c387456a8fcfded3ae4d1194d0e3e53a2031" );
            iv_len = unhexify( iv_str, "80b6e48fe4a3b08d40c1636b25dfd2c4" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "951c1c89b6d95661630d739dd9120a73" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "b865f8dd64a6f51a500bcfc8cadbc9e9f5d54d2d27d815ecfe3d5731e1b230c587b46958c6187e41b52ff187a14d26aa41c5f9909a3b77859429232e5bd6c6dc22cf5590402476d033a32682e8ab8dc7ed0b089c5ab20ab9a8c5d6a3be9ea7aa56c9d3ab08de4a4a019abb447db448062f16a533d416951a8ff6f13ed5608f77" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "b865f8dd64a6f51a500bcfc8cadbc9e9f5d54d2d27d815ecfe3d5731e1b230c587b46958c6187e41b52ff187a14d26aa41c5f9909a3b77859429232e5bd6c6dc22cf5590402476d033a32682e8ab8dc7ed0b089c5ab20ab9a8c5d6a3be9ea7aa56c9d3ab08de4a4a019abb447db448062f16a533d416951a8ff6f13ed5608f77" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "fa832a4b37dcb3c0879a771bb8ae734f0d88b9be497797a8" );
            pt_len = unhexify( src_str, "0f1105f9ec24121232b60b6ef3c3e8ca9eec1a3d7625004b857d1d77f292b6ec065d92f5bb97e0dc2fdfdf823a5db275109a9472690caea04730e4bd732c33548718e9f7658bbf3e30b8d07790cd540c5754486ed8e4d6920cefaeb1c182c4d67ebed0d205ba0bd9441a599d55e45094b380f3478bcfca9646a0d7aa18d08e52" );
            iv_len = unhexify( iv_str, "70835abab9f945c84ef4e97cdcf2a694" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "a459be0b349f6e8392c2a86edd8a9da5" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "dda216287910d1f5c0a312f63c243612388bc510cb76c5ba" );
            pt_len = unhexify( src_str, "d6617d583344d4fe472099d2a688297857215a3e31b47d1bf355ccfe9cf2398a3eba362c670c88f8c7162903275dfd4761d095900bd97eba72200d4045d72bd239bda156829c36b38b1ff5e4230125e5695f623e129829721e889da235bb7d4b9da07cce8c3ceb96964fd2f9dd1ff0997e1a3e253a688ceb1bfec76a7c567266" );
            iv_len = unhexify( iv_str, "7f770140df5b8678bc9c4b962b8c9034" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "9823e3242b3f890c6a456f1837e039" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "b4910277224025f58a5d0f37385b03fcd488dfef7580eb5c270c10bd7a6f6d9c7ddc2d1368d68d4e04f90e3df029ed028432a09f710be1610b2a75bd05f31bae83920573929573affd0eb03c63e0cec7a027deab792f43ee6307fd3c5078d43d5b1407ac023824d41c9437d66eeec172488f28d700aa4b54931aad7cd458456f" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "b4910277224025f58a5d0f37385b03fcd488dfef7580eb5c270c10bd7a6f6d9c7ddc2d1368d68d4e04f90e3df029ed028432a09f710be1610b2a75bd05f31bae83920573929573affd0eb03c63e0cec7a027deab792f43ee6307fd3c5078d43d5b1407ac023824d41c9437d66eeec172488f28d700aa4b54931aad7cd458456f" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "c5afa1e61d4594b1c2fa637f64f18dd557e4df3255b47f24" );
            pt_len = unhexify( src_str, "5c772cdf19571cd51d71fc166d33a0b892fbca4eae36ab0ac94e6164d51acb2d4e60d4f3a19c3757a93960e7fd90b9a6cdf98bdf259b370ed6c7ef8cb96dba7e3a875e6e7fe6abc76aabad30c8743b3e47c8de5d604c748eeb16806c2e75180a96af7741904eca61769d39e943eb4c4c25f2afd68e9472043de2bb03e9edae20" );
            iv_len = unhexify( iv_str, "151fd3ba32f5bde72adce6291bcf63ea" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "f0626cc07f2ed1a7570386a4110fc1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "febd4ff0fedd9f16bccb62380d59cd41b8eff1834347d8fa" );
            pt_len = unhexify( src_str, "dc971c8f65ece2ea4130afd4db38fc657c085ea19c76fef50f5bd0f8dd364cc22471c2fa36be8cde78529f58a78888e9de10961760a01af005e42fc5b03e6f64962e6b18eaedea979d33d1b06e2038b1aad8993e5b20cae6cc93f3f7cf2ad658fbba633d74f21a2003dded5f5dda3b46ed7424845c11bab439fbb987f0be09f8" );
            iv_len = unhexify( iv_str, "743699d3759781e82a3d21c7cd7991c8" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "1da347f9b6341049e63140395ad445" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "d280d079110c1c826cc77f490d807dd8d508eb579a160c49" );
            pt_len = unhexify( src_str, "a286d19610a990d64f3accd329fc005d468465a98cfa2f3606c6d0fbeb9732879bad3ca8094322a334a43155baed02d8e13a2fbf259d80066c6f418a1a74b23e0f6238f505b2b3dc906ffcb4910ce6c878b595bb4e5f8f3e2ede912b38dbafdf4659a93b056a1a67cb0ec1dbf00d93223f3b20b3f64a157105c5445b61628abf" );
            iv_len = unhexify( iv_str, "85b241d516b94759c9ef975f557bccea" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "bbf289df539f78c3a912b141da3a" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "b9286ab91645c20de040a805020fed53c612d493a8ce9c71649ae16bd50eab6fb7f3a9180e1651d5413aa542608d7ecbf9fc7378c0bef4d439bc35434b6cf803976b8783aecc83a91e95cea72c2a26a883b710252e0c2a6baa115739a0692c85f6d34ff06234fbdc79b8c4a8ea0a7056fb48c18f73aaf5084868abb0dfaa287d" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "b9286ab91645c20de040a805020fed53c612d493a8ce9c71649ae16bd50eab6fb7f3a9180e1651d5413aa542608d7ecbf9fc7378c0bef4d439bc35434b6cf803976b8783aecc83a91e95cea72c2a26a883b710252e0c2a6baa115739a0692c85f6d34ff06234fbdc79b8c4a8ea0a7056fb48c18f73aaf5084868abb0dfaa287d" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "5e80f87fa2156c62df7be2ad16c4890de5ee5868a684fcf9" );
            pt_len = unhexify( src_str, "c829073efd5c5150d2b7e2cdaeff979830d1aa983c747724ade6472c647a6e8e5033046e0359ea62fc26b4c95bccb3ac416fdf54e95815c35bf86d3fdd7856abbb618fe8fcd35a9295114926a0c9df92317d44ba1885a0c67c10b9ba24b8b2f3a464308c5578932247bf9c79d939aa3576376d2d6b4f14a378ab775531fe8abf" );
            iv_len = unhexify( iv_str, "9769f71c76b5b6c60462a845d2c123ad" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "394b6c631a69be3ed8c90770f3d4" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "f886bd92ca9d73a52e626b0c63a3daa138faaacf7809086d04f5c0c899362aa22e25d8659653b59c3103668461d9785bb425c6c1026ad9c924271cec9f27a9b341f708ca86f1d82a77aae88b25da9061b78b97276f3216720352629bd1a27ebf890da6f42d8c63d68342a93c382442d49dd4b62219504785cee89dffdc36f868" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "f886bd92ca9d73a52e626b0c63a3daa138faaacf7809086d04f5c0c899362aa22e25d8659653b59c3103668461d9785bb425c6c1026ad9c924271cec9f27a9b341f708ca86f1d82a77aae88b25da9061b78b97276f3216720352629bd1a27ebf890da6f42d8c63d68342a93c382442d49dd4b62219504785cee89dffdc36f868" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "d8a7b99e53f5e5b197364d4516cace4b928de50e571315e3" );
            pt_len = unhexify( src_str, "d0db0ac5e14bf03729125f3137d4854b4d8ce2d264f8646da17402bdad7034c0d84d7a80f107eb202aeadbfdf063904ae9793c6ae91ee8bcc0fc0674d8111f6aea6607633f92e4be3cfbb64418101db8b0a9225c83e60ffcf7a7f71f77149a13f8c5227cd92855241e11ee363062a893a76ac282fb47b523b306cd8235cd81c2" );
            iv_len = unhexify( iv_str, "4b12c6701534098e23e1b4659f684d6f" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "729b31c65d8699c93d741caac8e3" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "c874b427b7181b0c90b887147c36f242827149324fd5c945" );
            pt_len = unhexify( src_str, "bdd90190d587a564af022f06c8bd1a68735b6f18f04113fdcec24c6027aaf0271b183336fb713d247a173d9e095dae6e9badb0ab069712302875406f14320151fd43b90a3d6f35cc856636b1a6f98afc797cb5259567e2e9b7ce62d7b3370b5ee852722faf740edf815b3af460cdd7de90ca6ab6cd173844216c064b16ea3696" );
            iv_len = unhexify( iv_str, "4b8dda046a5b7c46abeeca2f2f9bcaf8" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "fe1e427bcb15ce026413a0da87" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "56543cd6e2ebb1e3dc136a826bfc37eddb12f7a26430a1b4" );
            pt_len = unhexify( src_str, "d541dd3acec2da042e6ea26fb90ff9a3861191926423b6dc99c5110b3bf150b362017159d0b85ffea397106a0d8299ec22791cb06103cd44036eed0d6d9f953724fb003068b3c3d97da129c28d97f09e6300cbea06ba66f410ca61c3311ce334c55f077c37acb3b7129c481748f79c958bc3bbeb2d3ff445ad361ed4bbc79f0a" );
            iv_len = unhexify( iv_str, "927ce8a596ed28c85d9cb8e688a829e6" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "3a98f471112a8a646460e8efd0" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "a602d61e7a35cbe0e463119bb66fd4bb6c75d1fe0b211b9d6a0a6e9e84b0794282318f0d33ec053f2cfba1623e865681affeaf29f3da3113995e87d51a5ab4872bb05b5be8ef2b14dfc3df5a48cbc9b10853a708ee4886a7390e8e4d286740a0dd41c025c8d72eda3f73f3cec5c33d5e50b643afd7691213cccccc2c41b9bd7a" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "a602d61e7a35cbe0e463119bb66fd4bb6c75d1fe0b211b9d6a0a6e9e84b0794282318f0d33ec053f2cfba1623e865681affeaf29f3da3113995e87d51a5ab4872bb05b5be8ef2b14dfc3df5a48cbc9b10853a708ee4886a7390e8e4d286740a0dd41c025c8d72eda3f73f3cec5c33d5e50b643afd7691213cccccc2c41b9bd7a" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "caaf81cd380f3af7885ef0d6196a1688c9372c5850dc5b0b" );
            pt_len = unhexify( src_str, "6f269929b92c6281e00672eaec183f187b2ddecc11c9045319521d245b595ab154dd50f045a660c4d53ae07d1b7a7fd6b21da10976eb5ffcddda08c1e9075a3b4d785faa003b4dd243f379e0654740b466704d9173bc43292ae0e279a903a955ce33b299bf2842b3461f7c9a2bd311f3e87254b5413d372ec543d6efa237b95a" );
            iv_len = unhexify( iv_str, "508c55f1726896f5b9f0a7024fe2fad0" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "3b8026268caf599ee677ecfd70" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "c4a96fb08d7c2eebd17046172b98569bc2441929fc0d6876aa1f389b80c05e2ede74dc6f8c3896a2ccf518e1b375ee75e4967f7cca21fa81ee176f8fb8753381ce03b2df873897131adc62a0cbebf718c8e0bb8eeed3104535f17a9c706d178d95a1b232e9dac31f2d1bdb3a1b098f3056f0e3d18be36bd746675779c0f80a10" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "c4a96fb08d7c2eebd17046172b98569bc2441929fc0d6876aa1f389b80c05e2ede74dc6f8c3896a2ccf518e1b375ee75e4967f7cca21fa81ee176f8fb8753381ce03b2df873897131adc62a0cbebf718c8e0bb8eeed3104535f17a9c706d178d95a1b232e9dac31f2d1bdb3a1b098f3056f0e3d18be36bd746675779c0f80a10" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "2fc9d9ac8469cfc718add2b03a4d8c8dcc2eeca08e5ff7bc" );
            pt_len = unhexify( src_str, "bc84d8a962a9cfd179d242788473d980d177abd0af9edccb14c6dc41535439a1768978158eeed99466574ea820dbedea68c819ffd9f9915ca8392c2e03049d7198baeca1d3491fe2345e64c1012aff03985b86c831ad516d4f5eb538109fff25383c7b0fa6b940ae19b0987d8c3e4a37ccbbd2034633c1eb0df1e9ddf3a8239e" );
            iv_len = unhexify( iv_str, "b2a7c0d52fc60bacc3d1a94f33087095" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "0a7a36ec128d0deb60869893" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "fc3cd6486dfe944f7cb035787573a554f4fe010c15bd08d6b09f73066f6f272ff84474f3845337b6e429c947d419c511c2945ffb181492c5465940cef85077e8a6a272a07e310a2f3808f11be03d96162913c613d9c3f25c3893c2bd2a58a619a9757fd16cc20c1308f2140557330379f07dbfd8979b26b075977805f1885acc" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "fc3cd6486dfe944f7cb035787573a554f4fe010c15bd08d6b09f73066f6f272ff84474f3845337b6e429c947d419c511c2945ffb181492c5465940cef85077e8a6a272a07e310a2f3808f11be03d96162913c613d9c3f25c3893c2bd2a58a619a9757fd16cc20c1308f2140557330379f07dbfd8979b26b075977805f1885acc" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "81ff729efa4a9aa2eccc37c5f846235b53d3b93c79c709c8" );
            pt_len = unhexify( src_str, "3992ad29eeb97d17bd5c0f04d8589903ee23ccb2b1adc2992a48a2eb62c2644c0df53b4afe4ace60dc5ec249c0c083473ebac3323539a575c14fa74c8381d1ac90cb501240f96d1779b287f7d8ba8775281d453aae37c803185f2711d21f5c00eb45cad37587ed196d1633f1eb0b33abef337447d03ec09c0e3f7fd32e8c69f0" );
            iv_len = unhexify( iv_str, "1bd17f04d1dc2e447b41665952ad9031" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "01b0a815dc6da3e32851e1fb" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "068500e8d4f8d4af9035cdaa8e005a648352e8f28bdafc8a" );
            pt_len = unhexify( src_str, "98e32428d9d21c4b60e690a2ce1cf70bee90df31302d1819b7d27fd577dd990f7ffe6ba5ef117caac718cc1880b4ca98f72db281c9609e189307302dc2866f20be3a545a565521368a6881e2642cba63b3cf4c8b5e5a8eabeb3e8b004618b8f77667c111e5402c5d7c66afd297c575ce5092e898d5831031d225cee668c186a1" );
            iv_len = unhexify( iv_str, "5ea9198b860679759357befdbb106b62" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "d58752f66b2cb9bb2bc388eb" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "2ef3a17fcdb154f60d5e80263b7301a8526d2de451ea49adb441aa2541986b868dab24027178f48759dbe874ae7aa7b27fb19461c6678a0ba84bbcd8567ba2412a55179e15e7c1a1392730ac392b59c51d48f8366d45b933880095800e1f36ff1ac00753f6363b0e854f494552f1f2efe028d969e6b1a8080149dd853aa6751e" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "2ef3a17fcdb154f60d5e80263b7301a8526d2de451ea49adb441aa2541986b868dab24027178f48759dbe874ae7aa7b27fb19461c6678a0ba84bbcd8567ba2412a55179e15e7c1a1392730ac392b59c51d48f8366d45b933880095800e1f36ff1ac00753f6363b0e854f494552f1f2efe028d969e6b1a8080149dd853aa6751e" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "7474d9b07739001b25baf6867254994e06e54c578508232f" );
            pt_len = unhexify( src_str, "1cbab2b6e4274caa80987072914f667b887198f7aaf4574608b91b5274f5afc3eb05a457554ff5d346d460f92c068bc626fd301d0bb15cb3726504b3d88ecd46a15077728ddc2b698a2e8c5ea5885fc534ac227b8f103d193f1977badf4f853a0931398da01f8019a9b1ff271b3a783ff0fae6f54db425af6e3a345ba7512cbf" );
            iv_len = unhexify( iv_str, "3ade6c92fe2dc575c136e3fbbba5c484" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "67c25240b8e39b63" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "d50d4c7d442d8a92d0489a96e897d50dda6fbe47ca7713ee" );
            pt_len = unhexify( src_str, "b36b4caf1d47b0d10652824bd57b603ec1c16f4720ce7d43edde8af1b9737f61b68b882566e04da50136f27d9af4c4c57fff4c8465c8a85f0aeadc17e02709cc9ba818d9a272709e5fb65dd5612a5c5d700da399b3668a00041a51c23de616ea3f72093d85ecbfd9dd0b5d02b541fb605dcffe81e9f45a5c0c191cc0b92ac56d" );
            iv_len = unhexify( iv_str, "41b37c04ab8a80f5a8d9d82a3a444772" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "4ee54d280829e6ef" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "38f3ec3ec775dac76ae484d5b6ca61c695c7beafba4606ca" );
            pt_len = unhexify( src_str, "49726b8cefc842a02f2d7bef099871f38257cc8ea096c9ac50baced6d940acb4e8baf932bec379a973a2c3a3bc49f60f7e9eef45eafdd15bda1dd1557f068e81226af503934eb96564d14c03f0f351974c8a54fb104fb07417fe79272e4b0c0072b9f89b770326562e4e1b14cad784a2cd1b4ae1dc43623ec451a1cae55f6f84" );
            iv_len = unhexify( iv_str, "9af53cf6891a749ab286f5c34238088a" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "6f6f344dd43b0d20" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "6db4ef061513ef6690d57aef50d8011e0dd7eb4432d82374" );
            pt_len = unhexify( src_str, "b7f9206995bc97311855ee832e2b40c41ab2d1a40d9263683c95b14dcc51c74d2de7b6198f9d4766c659e7619fe2693a5b188fac464ccbd5e632c5fd248cedba4028a92de12ed91415077e94cfe7a60f117052dea8916dfe0a51d92c1c03927e93012dbacd29bbbc50ce537a8173348ca904ac86df55940e9394c2895a9fe563" );
            iv_len = unhexify( iv_str, "623df5a0922d1e8c883debb2e0e5e0b1" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "14f690d7" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "a6414daa9be693e7ebb32480a783c54292e57feef4abbb3636bebbc3074bfc608ad55896fe9bd5ab875e52a43f715b98f52c07fc9fa6194ea0cd8ed78404f251639069c5a313ccfc6b94fb1657153ff48f16f6e22b3c4a0b7f88e188c90176447fe27fa7ddc2bac3d2b7edecad5f7605093ac4280b38ae6a4c040d2d4d491b42" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "a6414daa9be693e7ebb32480a783c54292e57feef4abbb3636bebbc3074bfc608ad55896fe9bd5ab875e52a43f715b98f52c07fc9fa6194ea0cd8ed78404f251639069c5a313ccfc6b94fb1657153ff48f16f6e22b3c4a0b7f88e188c90176447fe27fa7ddc2bac3d2b7edecad5f7605093ac4280b38ae6a4c040d2d4d491b42" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "8901bec4d3c64071d8c30c720c093221e05efed71da280bf" );
            pt_len = unhexify( src_str, "7c447e700db7367260dffa42050e612eff062eb0c8a6b4fe34858800bcb8ec2f622cb5213767b5771433783e9b0fa617c9ffb7fde09845dafc16dfc0df61215c0ca1191eabf43293db6603d5285859de7ef3329f5e71201586fb0188f0840ed5b877043ca06039768c77ff8687c5cfc2fd013a0b8da48344c568fce6b39e2b19" );
            iv_len = unhexify( iv_str, "9265abe966cb83838d7fd9302938f49d" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "6f6c38bc" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "2c57eb763f886154d3846cc333fc8ae8b3c7c9c3705f9872" );
            pt_len = unhexify( src_str, "9fe7d210221773ba4a163850bab290ba9b7bf5e825760ac940c290a1b40cd6dd5b9fb6385ae1a79d35ee7b355b34275857d5b847bef4ac7a58f6f0e9de68687807009f5dc26244935d7bcafc7aed18316ce6c375192d2a7bf0bee8a632fe4f412440292e39339b94b28281622842f88048be4640486f2b21a119658c294ce32e" );
            iv_len = unhexify( iv_str, "9b3781165e7ff113ecd1d83d1df2366d" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "62f32d4e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "307d31a594e54f673bea2f977835670aca4f3d45c9c376cc" );
            pt_len = unhexify( src_str, "d7385a7bd0cb76e1e242fa547c474370bcc7cc7cf3e3fa37b00fe08a56383ca31d023d8c493f6d42e482b0f32e4f244dd100ea08eee6535e5bb8d27f76dbb7eead6ba8e031ccd0eaeb649edee92aeaf0f027d59efd4e39b1f34b15ceb8b592ee0f171b1773b308c0e747790b0e6ace90fc661caa5f942bdc197067f28fbe87d1" );
            iv_len = unhexify( iv_str, "0bdaa353c4904d32432926f27534c73c" );
            add_len = unhexify( add_str, "aa39f04559ccc2cae3d563dda831fb238b2582cb2c2bb28cff20cc20200724c8771b9805ef7464b8fc06c7b8060c6920fd2779fbc807c2292c8c1f88f8088755609a1732ff8c0b06606452b970c79997b985889404fd907c4668a0bcc11ba617175f4525523494a244da60b238468c863055f04db20ea489adf545d56c0a71d8" );
            unhexify( tag_str, "2ddda790aae2ca427f5fb032c29673e6" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "0b92262759897f4bd5624a891187eba6040d79322a2a5a60fb75c6c6a5badd117abe40c6d963931bbc72dca1a1bf1f5388030fe323b3b24bd408334b95908177fb59af57c5cc6b31825bc7097eec7fec19f9cdb41c0264fd22f71893bcf881c1510feb8057e64880f1ea2df8dc60bb300fd06b0a582f7be534e522caadc4a2c7" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "0b92262759897f4bd5624a891187eba6040d79322a2a5a60fb75c6c6a5badd117abe40c6d963931bbc72dca1a1bf1f5388030fe323b3b24bd408334b95908177fb59af57c5cc6b31825bc7097eec7fec19f9cdb41c0264fd22f71893bcf881c1510feb8057e64880f1ea2df8dc60bb300fd06b0a582f7be534e522caadc4a2c7" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "23c201968def551817f20e49b09dbb5aae0033305bef68a0" );
            pt_len = unhexify( src_str, "77bc8af42d1b64ee39012df5fc33c554af32bfef6d9182804dcfe370dfc4b9d059bdbc55f6ba4eacb8e3a491d96a65360d790864ba60acf1a605f6b28a6591513ea3cfd768ff47aee242a8e9bdfac399b452231bfd59d81c9b91f8dc589ad751d8f9fdad01dd00631f0cb51cb0248332f24194b577e5571ceb5c037a6d0bcfe8" );
            iv_len = unhexify( iv_str, "bd2952d215aed5e915d863e7f7696b3e" );
            add_len = unhexify( add_str, "23f35fac583897519b94998084ad6d77666e13595109e874625bc6ccc6d0c7816a62d64b02e670fa664e3bb52c276b1bafbeb44e5f9cc3ae028daf1d787344482f31fce5d2800020732b381a8b11c6837f428204b7ed2f4c4810067f2d4da99987b66e6525fc6b9217a8f6933f1681b7cfa857e102f616a7c84adc2f676e3a8f" );
            unhexify( tag_str, "bb9ba3a9ac7d63e67bd78d71dc3133b3" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "17d93c921009c6b0b3ecf243d08b701422983f2dcaec9c8d7604a2d5565ed96ce5cddcb183cd5882f8d61d3202c9015d207fed16a4c1195ba712428c727601135315fc504e80c253c3a2e4a5593fc6c4a206edce1fd7104e8a888385bbb396d3cdf1eb2b2aa4d0c9e45451e99550d9cfa05aafe6e7b5319c73c33fd6f98db3c5" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "17d93c921009c6b0b3ecf243d08b701422983f2dcaec9c8d7604a2d5565ed96ce5cddcb183cd5882f8d61d3202c9015d207fed16a4c1195ba712428c727601135315fc504e80c253c3a2e4a5593fc6c4a206edce1fd7104e8a888385bbb396d3cdf1eb2b2aa4d0c9e45451e99550d9cfa05aafe6e7b5319c73c33fd6f98db3c5" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "6baec0669add30acb8f678ce477a2b171f89d1f41935c491" );
            pt_len = unhexify( src_str, "5712b84c4c97d75f84edd50561bc1d3f1ba451cc3b358b2403b5e528290954348cf7a235b4dc11a72ddbc503191204e98a9744d85419508c8ca76438c13305f716f1e239a6d9f6423c27217a0057aa75f6d7e2fb356e7194f271459ab5482589ea311b33e3d3845952ff4067dd2b9bcc2e8f83630b0a219e904040abd643d839" );
            iv_len = unhexify( iv_str, "b1472f92f552ca0d62496b8fa622c569" );
            add_len = unhexify( add_str, "5ae64edf11b4dbc7294d3d01bc9faf310dc08a92b28e664e0a7525f938d32ef033033f1de8931f39a58df0eabc8784423f0a6355efcff008cae62c1d8e5b7baefd360a5a2aa1b7068522faf8e437e6419be305ada05715bf21d73bd227531fea4bc31a6ce1662aec49f1961ee28e33ae00eb20013fd84b51cfe0d5adbdaff592" );
            unhexify( tag_str, "29a2d607b2d2d9c96d093000b401a94f" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "beb687f062ae7f5159d07609dd58d7b81c478d180bc0b4c07ae799626ff1da2be2e0d78b2a2a1f563257f161491a5ac500cd719da6379e30d0f6d0a7a33203381e058f487fc60989923afbee76e703c03abc73bb01bd262ff6f0ac931f771e9b4f2980e7d8c0a9e939fa6e1094796894f2c78f453e4abe64cb285016435ef0e8" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "beb687f062ae7f5159d07609dd58d7b81c478d180bc0b4c07ae799626ff1da2be2e0d78b2a2a1f563257f161491a5ac500cd719da6379e30d0f6d0a7a33203381e058f487fc60989923afbee76e703c03abc73bb01bd262ff6f0ac931f771e9b4f2980e7d8c0a9e939fa6e1094796894f2c78f453e4abe64cb285016435ef0e8" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "7b882a2df81fdb9275fb05d120f32417e8ffedd07457e938" );
            pt_len = unhexify( src_str, "0aae7213da279b34d6dcf2a691b2d0333112ea22de0c3c68d47cf9f9f4ed8ad4e03d4a60ec18c3a04ac9c2abb73e1023051029b5e8705bb69c4c50afc84deb0379db5077be1f663652f8bd8958271af2c1ac4a87e08cb526bab8a030652f2a29af8055d0f31e35475caee27f84c156ef8642e5bfef89192f5bde3c54279ffe06" );
            iv_len = unhexify( iv_str, "5c064d3418b89388fb21c61d8c74d2c5" );
            add_len = unhexify( add_str, "5bfa7113d34e00f34713cf07c386d055e889bb42d7f6c8631ffce5668e98cb19bed8820b90ecb2b35df7134f975700347e5514287cfef7ffa2b0ff48b1de0769b03dca6610995d67cb80052cb2e5914eb4ed43ef5861f4b9364314fde6ad2b82fbba7fd849dfa6e46ecc12edc8cabfff28d9bd23c2bcc8ab3661c9ba4d5fee06" );
            unhexify( tag_str, "0943abb85adee47741540900cc833f" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "51d94d21482c00bb5bc7e7e03aa017ba58f5a23494b72c2a" );
            pt_len = unhexify( src_str, "3a9c69c1ed2340bfde1495658dbf4f54731a19b3922a1d535df8d0b2582f5e803b5891e8ad1aa256c923956dcda2430d0c0696bce63295fb61183e040566e459338f908d23ae51f64020c1ef3d192428f23312b285fc4111d50d1add58f4a49008a22c90d3365230e9158cd56f9d84f079bdd673555d4dc76c74b02fa9920e7d" );
            iv_len = unhexify( iv_str, "fb21cd763e6f25540f8ad455deaccdf0" );
            add_len = unhexify( add_str, "019d1db5569eeff83306f65d653b01064854c1be8446cd2516336667c6557e7844fc349adea64a12dc19ac7e8e40b0520a48fac64571a93d669045607085ac9fa78fed99bbf644908d7763fe5f7f503947a9fe8661b7c6aef8da101acca0aed758ca1580eeb2f26ae3bf2de06ce8827a91a694179991a993cdf814efbcc61ca5" );
            unhexify( tag_str, "a93bd682b57e1d1bf4af97e93b8927" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "7093f44703f2cbb3d12d9872b07a8cd44deb62dae48bc573b11a1ee1c9f3105223423fac3181c312a8a61757a432d92719f486c21e311b840aa63cf530710c873df27fecda0956075923f1ecc39bffb862706f48bde2de15612930fc8630d2036e9e4cfc1c69779171bd23d9e1d5de50a9e0a0de4bd82ed3efc45299980bb4cc" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "7093f44703f2cbb3d12d9872b07a8cd44deb62dae48bc573b11a1ee1c9f3105223423fac3181c312a8a61757a432d92719f486c21e311b840aa63cf530710c873df27fecda0956075923f1ecc39bffb862706f48bde2de15612930fc8630d2036e9e4cfc1c69779171bd23d9e1d5de50a9e0a0de4bd82ed3efc45299980bb4cc" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "e6756470937f5d9af76f2abe6df2d0bc15ff8e39b5154071" );
            pt_len = unhexify( src_str, "afae92bd56c426c095d76633701aa9bea5ce05490482c6c64ac24468c3e1af6e6030a6bb6649745b011c6729bde985b9242e22105322fbb8853dcabbd00165d0b07d7b499e0238b6513bf6351eb40635a798f7e6e2d31125dda45ffe8964596fdbff55df22d4e9025bd4f39e7c9b90e74b3ee58d6901f113900ee47a4df5afd7" );
            iv_len = unhexify( iv_str, "4500193711a5d817a9f48deafda39772" );
            add_len = unhexify( add_str, "92fa22dba0eee6b1de1ddd24713b1be44c7105df90e6e7a54dcbf19025e560eb4986ee080cf613898a1a69d5ab460a3b8aa2723a95ac4a4af48224b011b55fb7582ae18f6746591eab2bd33d82a8dbbae3f7877e28afef9857a623530b31d8198b2df43f903d6e48ddae0848741f9eaae7b5504c67ad13791818f3c55c9b3d1e" );
            unhexify( tag_str, "7d9f97c97c3424c79966f5b45af090" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "62258d60f0138c0405df4b2ec1e308b374603a9eace45932fdc2999e9e2261de8b1099473d1fc741c46c334023aa5d9359f7ef966240aaf7e310d874b5956fd180fb1124cbeb91cf86020c78a1a0335f5f029bd34677dd2d5076482f3b3e85808f54998f4bac8b8fa968febceec3458fb882fc0530271f144fb3e2ab8c1a6289" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "62258d60f0138c0405df4b2ec1e308b374603a9eace45932fdc2999e9e2261de8b1099473d1fc741c46c334023aa5d9359f7ef966240aaf7e310d874b5956fd180fb1124cbeb91cf86020c78a1a0335f5f029bd34677dd2d5076482f3b3e85808f54998f4bac8b8fa968febceec3458fb882fc0530271f144fb3e2ab8c1a6289" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "30db73d46b518669c45b81bc67b93bed3d0864f7e9e8e789" );
            pt_len = unhexify( src_str, "750bc1d2f91d786bb1e621192a376f552538ba8c07d50d9e10b9345f31b3e5f9d8ad7c719c03d8548a3b184b741cd06c49d7fb6fe80258d60c01c2987c337c823211cee7c1cf82077266889bc7767475e0eeabb2ef6b5a1de2089aaef77565d40a1c2c470a880c911e77a186eacca173b25970574f05c0bdcd5428b39b52af7f" );
            iv_len = unhexify( iv_str, "5069e2d2f82b36de8c2eb171f301135d" );
            add_len = unhexify( add_str, "ef781dce556b84188adee2b6e1d64dac2751dd8592abc6c72af7b998dfae40cbe692a4cae0b4aa2c95910e270600550fca1e83640c64efb1eb0e0a90a6fc475ae1db863a64ce9cc272f00abac8a63d48dd9f1c0a5f4586224befed05be4afae5bd92249833d565cc6b65fd8955cb8a7d7bd9f4b6a229e3881212871a52c15d1c" );
            unhexify( tag_str, "a5100c5e9a16aedf0e1bd8604335" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "209f0478f1a62cb54c551181cbd4d24b796e95f3a06b6cb9" );
            pt_len = unhexify( src_str, "66db7cc97b4a8266c0a2228e8028e38d8986e79fcbcc3caff3050fdd2de87b7ff7a6895b988b0bdb7fcc4d6e2d538dcfaad43ce2f98b6d32500f5a6e6183d84cb19157a699cdde1266d6d75a251ee1a2eb97bfe6405d50be2b17a58ba6eafaee0a023a28d568fd1c914f06041a49c79b9df9efe63d56883cbbbeaba809273d2e" );
            iv_len = unhexify( iv_str, "7be1768f6ffb31599eb6def7d1daa41c" );
            add_len = unhexify( add_str, "9cb49357536ebe087e1475a5387907a9e51ad1550697f13c6cc04384ec8a67dea13376bdd5e26b815c84a78f921b506b9e2086de50f849185f05ba7c3041e49e42c0673df856da109a78b8e0ce918c25836f7e781e6b16168e4e5976d27ebc83f20b7bf4beadecb9b4f17a7a0d3a3db27fc65288a754b5031a2f5a1394801e6e" );
            unhexify( tag_str, "4d2ac05bfd4b59b15a6f70ea7cd0" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "1bfa30b315e7b908263330140fa2d66ed57104784a43cc70" );
            pt_len = unhexify( src_str, "8eeee9865e23fa51dbbf197fa41776b7edbdb9381a22c935299cd959a46190788ae82f4e645b0362df89bfc00241964784bc7ef70f6f97e81687d52e552a33af20ae34a3005e0a7b85d094368d707c3c4cd3ef31c0daf3ccaa1676609ed199327f4139d0c120977e6babceed28896d2cb3129630f3ee135572dc39433057e26a" );
            iv_len = unhexify( iv_str, "b7081a3010b524218390ba6dd460a1ec" );
            add_len = unhexify( add_str, "8c1f42b5931d69ae351fcde7d2b4136d4898a4fa8ba62d55cef721dadf19beaabf9d1900bdf2e58ee568b808684eecbf7aa3c890f65c54b967b94484be082193b2d8393007389abaa9debbb49d727a2ac16b4dab2c8f276840e9c65a47974d9b04f2e63adf38b6aad763f0d7cdb2c3d58691adde6e51e0a85093a4c4944f5bf2" );
            unhexify( tag_str, "4da85b8ec861dd8be54787bb83f1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "fc47156a693e59a1dea0618c41441fe669fc65dcfb7d0726" );
            pt_len = unhexify( src_str, "3e4f0a586bad532a08c8863ebba01fd25014baa907e6032ee43d4a7dfc7c3171916dcdf9faee0531f27527872ae4e127b6b9aaee93f5e74d0ab23f3874aa0e291564bc97f17085dd7d5eb9a85d9f44574e5952929eda08863b64c85dd395c91b01fe5bef66e3fa8f9ee5bf62c25d80dc84fbe002ecfd218430b26f3549f734a1" );
            iv_len = unhexify( iv_str, "ea1935ed014883cc427983d7962d9992" );
            add_len = unhexify( add_str, "0d85b8513becfe8c91d0f6ffb65ec31f2cf406c51c0da88893c43d1327fd8ad1f4bab2d7b5e27438d643397034a72f8666bf641b6781bc90f764db387eae6720b5723d510194570ccd773e1b3bebfc333cc099d078583e8dac60d174d332925a24a45110c8d2abe8924ea677ac74db66ea789e2838efc96c78bceaa6236c0a67" );
            unhexify( tag_str, "8781b045a509c4239b9f44624e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "b5fcd780a03ba80341081ef96b440c0e4348afde4d60c1d5" );
            pt_len = unhexify( src_str, "6316f3beb32f6f3bf8f2ff6a2c160b432bafd3036d3eefa1e4ec204f24892e37dc4d75c7ce9a24b5c49fb4df901f35ef9d5955f7dc289c56cb74753f4d6b2982267d5269d12237e21202a65061849c65e90e6702dda03a35ace3a3a098d16b4bfbb85b7232404baee37776a9b51af6b3059a5f170f4ebe4ecf11061ca3c1f1f3" );
            iv_len = unhexify( iv_str, "ad20cce056e74ec5d0a76d6280998f15" );
            add_len = unhexify( add_str, "28f8fcf23b9c1ba40c19ffc1092632e35f234c1e8b82bcd5309d37bf849a2ce401413d1f242cf255ed597f9a93a1d6e50676997f95aa612e580d88234a86ddc404292746f0b2f5cf15abebcea6659f998ec6a1cb5a9914fee5aa1aa5d04b3c20914e45095e4141ce9c173653dd91c3ebe4ed4a9a28f3915d7b2edba34c2a58d8" );
            unhexify( tag_str, "2ad4520ddc3b907414d934cc1d" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "4382507dddccf1385fc831da8924147563416d0656e168ec" );
            pt_len = unhexify( src_str, "e5c5430b960aa35dc8540215c2772d66811270859e33dd4477904759e7e5eb2986a52a4ccc9f592e614147b5ea2ead6636a15c6426336b2995d9a31ab36d76578c3540bc6693842a4bc0491c7963ee9cda2317951cf93244bd30bcdfec69a4767004636fe7d1be7300c35e80627bab9236a075a803e9e1080b9159060c643a78" );
            iv_len = unhexify( iv_str, "a37687c9cd4bdc1ead4e6b8f78bee7f5" );
            add_len = unhexify( add_str, "fa9ae30509cbb6fe104c21480ae7b8ec9f12f1afb17320d77b77cdf32ce8c5a3f7f927e501118c7ccd6975b79225059cef530a4fcb0a9719f5e2d3bebe7bb6ec0855e495a31e5075eb50aa6c1227e48b03e3fdf780084ac4912eb3a5674cca9dd6ac037366b230ae631a8580d2d117942dee5d5ddbbb2233afeca53289cc4f68" );
            unhexify( tag_str, "4221818d4be45306e205813789" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "b5b36719bc4d13a5fbf37188ea814cdf3c97a430784330540325c899570e15482300bc82c5b8163074e0544c5132e3ce93bba68bd7a8d2db81d1431b424b697c1158c4d70625666d5ff99145ca34856815c905b5a0fd95806df56b9cd5b384bda3e394b409048eb1037144cc071539c02397e931da28a43cc354d584643afd4f" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "b5b36719bc4d13a5fbf37188ea814cdf3c97a430784330540325c899570e15482300bc82c5b8163074e0544c5132e3ce93bba68bd7a8d2db81d1431b424b697c1158c4d70625666d5ff99145ca34856815c905b5a0fd95806df56b9cd5b384bda3e394b409048eb1037144cc071539c02397e931da28a43cc354d584643afd4f" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "7a66db3450dac9a1e63d2639f34c5c6a3fbfb3c8e8230199" );
            pt_len = unhexify( src_str, "6463a7eb2496379bc8a5635541525926a6f9fa718e338221952118ae4cf03a85f2074b4ebaf108b9c725809be1e6309c3a444b66f12286f6ea9d80c3413706b234b26372e8f00783819314a994c9e3ecf6abdd255cbfe01b3865e1390a35dcd2853a3d99ed992e82ec67ba245f088cb090adade74bdbc8a1bad0f06cbea766a6" );
            iv_len = unhexify( iv_str, "21f8341529b210ade7f2c6055e13007a" );
            add_len = unhexify( add_str, "1699bc8c198ab03e22d9bc4f3682aad335c6e35f3f616bb69769a9d5a202511797e770ae0d8d8528ef7b2bb25b4294d47427b43f0580fa71d93fdef667f4f4196f84e41c0b1978796d0de74a94420fb8571bff39137fa231c572b31be9ae72338288bef5f8c992121dc918538551f346e279a9047df14ec9fc0fd399cd3bd8d8" );
            unhexify( tag_str, "4af02b81b26104d1d31e295a" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "53fe6a34d280f2c96d1ae2b2e8baf6abd67cedf7d214312f75dd4a1bec28a641dda3e71aa398726b2b0b1f515e1f4259ee97acaf17f122db9ec7814c2de6a88d36c3ac106396ad03d337c2cd2d2b9b4b7170e23a5848ca7ea129838f967dfdfe83b45ff2a9be699bfb2346115465d59f074f09e24d8fcbd9ece0018c92776c43" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "53fe6a34d280f2c96d1ae2b2e8baf6abd67cedf7d214312f75dd4a1bec28a641dda3e71aa398726b2b0b1f515e1f4259ee97acaf17f122db9ec7814c2de6a88d36c3ac106396ad03d337c2cd2d2b9b4b7170e23a5848ca7ea129838f967dfdfe83b45ff2a9be699bfb2346115465d59f074f09e24d8fcbd9ece0018c92776c43" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "1f5c818f24d201f9fb23fcca211b0545eee5c5c9b440810d" );
            pt_len = unhexify( src_str, "9a7566817a06f792e96a6a2ba8e0a01f8837e2de06796e68b0782cc54ed0b04fc5e24a1ad37d5ffb035548b882d88150e89915b89f57cde2bf3c43ab9dae356927daef6bd61cc9edd5e1b7a4abea2f71313677f1b2fdf3d8d4a7e9814ea820fbc3e5c83947db961839a985a57ced7f5e4a1efffcfd17a2c806d4cdc1e79162da" );
            iv_len = unhexify( iv_str, "3a163067bdd90fce0406d1c198a88771" );
            add_len = unhexify( add_str, "a5e94e233d04fe0c4b6c4684b386902fe05096702237dfbe76f73befa69b6f30394cf9fe3358997942df65842748fb4f075a3dc06e147bd8d67fc4371113a4d75c70219257c650a6f38a136659e20a1cf3a119397835c304e0fb2a33aa3c3019175c86463043d5edc6992874f61e81cd0d26af8b62cf8c8626901d4f16d84236" );
            unhexify( tag_str, "b124eea927e2a62a875494a1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "9a301f7edf83da63bcf37216a3a33d7613331c3210281dd7" );
            pt_len = unhexify( src_str, "e09cc8543db7804870004706a26e94b457c125bd648b581a196f962f2ae8fa55d9bc66530ba5020e22d282080b4720dc9a2096a11c0fcc3d9a67cd1cf95cd7cd2417ba308c761e64be24347a14c9423447094a5c72a0043c288b35e753ba0aa748f208381249fb1c8d195a472192404b6c8172663ee4b4d4ecfa426e1fb003f2" );
            iv_len = unhexify( iv_str, "d73a546b0fa307633ac89506fa86138b" );
            add_len = unhexify( add_str, "f57fe548cf4a551a216ffb24a1dcf1b79c95f9abf06443fd58af042d287c2165db373c82a94172db517840f22e45e966e3ead91ce1ddad132bcb844e406e84b76a0b5b0ee23064b66a229f32a2d3b9c71103f020c4ba57fc0f0608b7114914cf2ada0c5a9bc4afbfa9ce5da320f34beb2211d569a142f53bfd262f6d149c4350" );
            unhexify( tag_str, "f536a3b8c333b1aa520d6440" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "124a327a8c22b7652886dac2c84b8997ca8a6f61c9ba9c094b5aea41eaa050a6df6cbf280259e5466071bcfa53b4ebc76c3cc4afc8c0385189a5382933aa57c89aab78dca84331e0fe8f0aab3a7857d3e13f08dcd90ec5f0684f82088ef8eb7fd67e75de43b67afc3a0beb458f5ebd61b2c779e6c539d795c667bb7dcc2b762e" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "124a327a8c22b7652886dac2c84b8997ca8a6f61c9ba9c094b5aea41eaa050a6df6cbf280259e5466071bcfa53b4ebc76c3cc4afc8c0385189a5382933aa57c89aab78dca84331e0fe8f0aab3a7857d3e13f08dcd90ec5f0684f82088ef8eb7fd67e75de43b67afc3a0beb458f5ebd61b2c779e6c539d795c667bb7dcc2b762e" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "fd40e8226fd13cb95ba50b7cdf0f07f7ab7037cf8705ca50" );
            pt_len = unhexify( src_str, "75aa7df5c3c443d48ee998064b6fd112c20d2d90c98e00d025ef08d1ad3595385be99de47fa627549b827c48bc79eb1dcaf2f1be95a45f7e55755b952aee5ae0748e68bee1b014a628f3f7dc88e0ebac1d1d00e268355f5101838ce125c57003aebc02a1c9d6ae2cd6e2592f52c0be38cef21a680ae35c909cab99dce9837aef" );
            iv_len = unhexify( iv_str, "3406e70cbe16b047fedaa537eb892279" );
            add_len = unhexify( add_str, "390b18d22d5ecc0b5a524ae9afac6fd948ac72d1360775a88b385aa862cce8a27f3e4b420e539bec6e8958f8c1b5416c313fa0a16f921149a2bfeae29ad2348949b29a73970e5be925ec0c35218b82a020cf21bb68c6931f86b29e01b85500a73f3ee7eb78da60078f42550da83b2e301d151d69b273a050f89e57dfc4787cbf" );
            unhexify( tag_str, "69e06c72ead69501" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "6e8d661cd320b1b39f8494836fcf738b0ab82873d3903c9ee34d74f618aea36099926b54c1589225ec9a9d48ca53657f10d9289c31f199c37c48fb9cbe1cda1e790aaeedf73871f66a3761625cca3c4f642bc4f254868f6b903e80ceeeb015569ace23376567d3712ad16d1289dc504f15d9b2751b23e7722b9e6d8e0827859f" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "6e8d661cd320b1b39f8494836fcf738b0ab82873d3903c9ee34d74f618aea36099926b54c1589225ec9a9d48ca53657f10d9289c31f199c37c48fb9cbe1cda1e790aaeedf73871f66a3761625cca3c4f642bc4f254868f6b903e80ceeeb015569ace23376567d3712ad16d1289dc504f15d9b2751b23e7722b9e6d8e0827859f" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "a85ab87563b809b01725764d64ba4cc6a143e2e0362f0c52" );
            pt_len = unhexify( src_str, "ef43629721b50bd3656b7ae31b6e4b4ba1cf2c72ed0460ee7d9fb416631ddc597e5f9aebbcf4442b95cc46e28476a464dd87caf9c1c1d6c99d3e3e059dc23f8d2fe155ff5e59c50d640bc052c62adee3aa1295b38732e3458f379e98a8dbdfed04c22a5761792e87fa67ecbcbf3b90eb1bcd1d3f49e60132452f28afece83e90" );
            iv_len = unhexify( iv_str, "9f991ff16a3e3eb164a4f819c9f1821a" );
            add_len = unhexify( add_str, "df289511f78d8fa2505afc4c71ab1d7c31a8d15d1e5fcbb29d70f0e56f89c4d7b30f1b3b4745b5d2cc7af34fb4c95461372bf516ec192b400dc8fdb0ca9fe1f30f5320d0fadf20155cfcddcf09233c6f591c1c89917e38a003f56b94a1e2429d1f2b6297db790d7dce84d9fa13d2d86a0e4d100e154050b07178bee4cdf18126" );
            unhexify( tag_str, "dc4c97fe8cc53350" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "ff0e531c7344f0425d62d5fbedf4bc8d3d5cc80647e67b852c1a58ad1516d376d954cb8dda739f6a4df3cf1507e59696610bcb6b34340d6313028e00d7197845d392e73331aaf168b474a67364d8f9dab740509fabf92af75045f0afabc1b5829264d138820952bbc484d1100d058a4de32b4ece82746b2b4a85fb2993d4add8" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "ff0e531c7344f0425d62d5fbedf4bc8d3d5cc80647e67b852c1a58ad1516d376d954cb8dda739f6a4df3cf1507e59696610bcb6b34340d6313028e00d7197845d392e73331aaf168b474a67364d8f9dab740509fabf92af75045f0afabc1b5829264d138820952bbc484d1100d058a4de32b4ece82746b2b4a85fb2993d4add8" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "f4f1e03abb927ffd0b081b9dce83a56a6dd419a6313ac34f" );
            pt_len = unhexify( src_str, "0e70421499bc4bcb3851afa34cdf5be374722815abdd9bcee5f332dbe890bdc1c0210ab10667e5bb924bf3c1120e25a0c074da620076f143940989e222086d1b34a1200d09aea1f810ef6de7d8520c65eef9539fde5a6422606c588fce6264e5f91f934ede6397c4b307d2d7e07a518fce577a427fa92923cbba637ae495afad" );
            iv_len = unhexify( iv_str, "d1e29bb51a3c4e871d15bb0cd86257e2" );
            add_len = unhexify( add_str, "ae2911cdaaad1194c5d7868b6d8f30287105df132eb0cecca14b6e23ec7ac39cc01da1c567a0219cca7b902cc2e825e30f9524a473eb6e1d4d1beff5ab4f29103b2c7522a33dd33182fa955c4f09a75196b1072a6f0340fc55a802d29c7067f05219c21857ebff89ada11f648c1f28dfbfdaab56028f05509de17e2381457ebc" );
            unhexify( tag_str, "44f760787f7bc3c0" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "2199fa5051461b67581429ab19de2ccb50b8b02e12c0e1d81a8a14929f84e09d9715b7d198e77e632de4af1c08c5041276204a7ed76646385e288e96e1a4b0b0f2b1a9df7f0892beaea3cb58d9632720158f6daa4cbbfc0ebdc56ff6a5175768ff2abd24cb7669bc3fe40f8aba7869d2dd7dac86b6ebc4e4ce261edbec88db17" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "2199fa5051461b67581429ab19de2ccb50b8b02e12c0e1d81a8a14929f84e09d9715b7d198e77e632de4af1c08c5041276204a7ed76646385e288e96e1a4b0b0f2b1a9df7f0892beaea3cb58d9632720158f6daa4cbbfc0ebdc56ff6a5175768ff2abd24cb7669bc3fe40f8aba7869d2dd7dac86b6ebc4e4ce261edbec88db17" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "33efe20433c6a1ad261a1fed494961749e5bf9d35809b59d" );
            pt_len = unhexify( src_str, "cfbeb61be50def25f513346498f75984bfe797a8ad56be34f2461e2d673f6ce14e7479a59777267b75dadc6b9522599ebe5d7b079495a58ca187ec47796f6ee8c322278ad7451b038c938928adcff6105a8ea3780aedc45b6a3323d3ae6fbce5da4fb59ca5ec0a16a70494c3c4859672348532505e44f915e0b9b8a296ef5225" );
            iv_len = unhexify( iv_str, "dc94673b0c49c6d3b4611e278212c748" );
            add_len = unhexify( add_str, "919f7397a6d03836423b7cac53177fcfbe457d4aa4348646f646aae1bc5a15568cdb8c96fabef278ace248aca531110a4f4f9e8ab0c32525ad816ae3facf03175232dc84addcd6065f9cc1f513966b63fd27e91a09f1921b95d6bd8f08f1dbce073bcf827847f774514b478b9d7fb5426847dd4dee6f39b5768c1fb729b32d03" );
            unhexify( tag_str, "c5098340" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "c5e47d8c60b04df1974b68a14095d9bc8429a413d21960b15bae4fd7356bf7872e0da0a1a385ca2982d3aa3182e63ea4bb8ca01410cd4e71ddad34aa1f12c1387902b3d56634f89c619a2e6756648ab3bf90e9bc945afc9140eb935b633bae96bb067e9ee421697bcf80b14b1b88dbf13e010b472a7ca5411db36848b9c7a37f" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "c5e47d8c60b04df1974b68a14095d9bc8429a413d21960b15bae4fd7356bf7872e0da0a1a385ca2982d3aa3182e63ea4bb8ca01410cd4e71ddad34aa1f12c1387902b3d56634f89c619a2e6756648ab3bf90e9bc945afc9140eb935b633bae96bb067e9ee421697bcf80b14b1b88dbf13e010b472a7ca5411db36848b9c7a37f" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "3ed5dadefa0f6d14fedd1a3cdbab109f6660896a952ac5ab" );
            pt_len = unhexify( src_str, "aef617f69724e020309ec39d9587520efda68a8e303686c3a41ef700cba05b7c6e43e95aadb1a566f61650c87845835e789eb2366941e3bfef6d9846af0e0dbc43249117ad6f299bbc40669ac383cdf79289ada6ccd8ccfe329a0dc6a38eea1a99550457102d10f641cda50c21f533b1f981663f74a0a7c657c04d9fc6696ff4" );
            iv_len = unhexify( iv_str, "553a14f1e1619f9d7bd07cd823961f25" );
            add_len = unhexify( add_str, "eb8ea81d3e328a1113942cd5efd0f2b5e7f088791c8fc05690a34584101c4d493628ee7d0099a2865ac194b9124c3fb924de0c4428d0a1c26ea3ad9a0bc89187a16673e3b6f7e370dfb2dc26e8a56a9cf91f9c2088c020a766efe0d0c91689743a603f2cd1e300a6a84828b3b515a4b9a06e6bb20457bf124cd6ce4ac8b83d51" );
            unhexify( tag_str, "dc413c4c" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "bc1f34991a48aabb0fea513f790f0d223e9feac4c99fa1e8427f01ab8b4b2827cfaf239342de36051a846af0306a3f82e7aed98dd0416fb078bc7f3b617b00ceb2cea4ddafc22dd022efa8303e9804510e0e888065d8427345156d823f796f74130c06db9f9934435552b4fefd051953e20ecba3a4514ac121d7d2097d597439" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "bc1f34991a48aabb0fea513f790f0d223e9feac4c99fa1e8427f01ab8b4b2827cfaf239342de36051a846af0306a3f82e7aed98dd0416fb078bc7f3b617b00ceb2cea4ddafc22dd022efa8303e9804510e0e888065d8427345156d823f796f74130c06db9f9934435552b4fefd051953e20ecba3a4514ac121d7d2097d597439" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "6d97e8bff3923a778504fb917dbc1428a1328587047697d9" );
            pt_len = unhexify( src_str, "dc1a81efd51e967767f5bdd7e2e425732c1d28451f2bf5bdf3f5a6492279330594d360dd8a193e5dbde1be49bf143a35c38bcd059f762ada65c5119e097f0976891347f4d829b087bd72daa3494b344cbd3370c4459ca243bd57aeda4cb86cdd0bf274f07830cdbf5e5be4eb9b742ddffef8aa35626d2b9ea0a29d3c3d058b28" );
            iv_len = unhexify( iv_str, "0c28dc4cd53725091c2fb68a476c2e40" );
            add_len = unhexify( add_str, "f3932f5e82d75a1e3eba1591c17769e1a45819ccf057c31e76fa810b93678766d25905e859775c244e96bcafbc75c4a2d95e7d02868ccb2f65e49276f0b645ac8cf6e3758402304a3c25ce2de0a49f401b1acadaff8b57589b45cc79130ddc8387f41cc383e33ef38eec019152051c756198d6f782ccf56297b9fe944269a65a" );
            unhexify( tag_str, "e6d6df7a" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "39327836e9d8cfb59397adcf045a85644c52c3563290795811f26350c8bce8f55ca779cbcd15479efd8144b8a39ef611153955c70bf3a7da9d4d944c2407a0d735784fcb68de1083eebf6940ebc9cf92f9f139c01404b503ff64e61126a94e881351473507884357040fd32714b872c254349071069644e2bd642905521b944e" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "39327836e9d8cfb59397adcf045a85644c52c3563290795811f26350c8bce8f55ca779cbcd15479efd8144b8a39ef611153955c70bf3a7da9d4d944c2407a0d735784fcb68de1083eebf6940ebc9cf92f9f139c01404b503ff64e61126a94e881351473507884357040fd32714b872c254349071069644e2bd642905521b944e" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "2c78e29971e90a01bb65973f81260b9344fa835751f5f142" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "f1a23ce6e2bc9088a62c887abecd30ae" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "d4d5c22f993c8c610145fcbe4e021687" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "8c582d5b6a40ef0e4048ec20f0263572d7cc82704e380851" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "ef221a1c66fda17906190b7c99ab60b8" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "6327dcb46ffb3d0fd8fbf3d2848a8f01" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "3a58abadd29e946e23ca9eb09af059913d5394971bda6a4f" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "7c29b3196d44df78fa514a1967fcd3a6" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "fc123944bbea6c5075a5f987aed9cf99" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "04bdde4c35c385783715d8a883640851b860ce0e8436ec19" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "783f9a3c36b6d0c9fd57c15105316535" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "23e21a803cac5237777014686564f2" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "4ba5fba0c22fbe10c2d1690c5d99938522de9c5186721bac" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "2acc2073089a34d4651eee39a262e8ae" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "7ac742c859a02a543b50464c66dcf5" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "f12890b0a8819faa5a8e0e487f7f064af42fa6d5519d009f" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c937615675738f4b3227c799833d1e61" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "88300bd65b12dcb341f1f6d8a15584" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "51878f3630298a81297f4a21514fea637faa3815d4f26fae" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "1f939226feab012dabfc2193637d15b1" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "eed5fcb7607c038b354746d91c5b" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "ae596e74840a600556a06f97b13b89e38f67c152f1a1b930" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "e2076e1050070d468659885ea77e88d0" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "b4586bdbd4b6b899648f2333eee0" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "fd33b7a0efae34339ca987b5eb8075385fd1276e63cc8530" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "2d07bb8616fc0bbb71755a1bd256e7fb" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "6b60d645220cfde42d88296ac193" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "5685b12a6617d554c36b62af5b8ff2239cb3ffb1d2c40e14" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "6c31194df99d08881fa5b1dd33b45a92" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "69431593c376c9f8052bf10747" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "036ae037410dae9f0741608516d03b855c9c1851df8c54a4" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "73599275f8237f14c4a52b283c07275d" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "6f7249d25c9f273434c4720275" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "ac144f39ebd6124bad85c9c7fb4f75bff389ece2e8085d83" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "d0871bfc3693245be478e6a257c79efb" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "5a99d59631d0e12f58b7b95ccd" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "a8a541ff11a1b8548e832d9e015edeccc94b87dadc156065" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c72bb300b624c27cded863eba56e7587" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "ea2528e7439be2ed0a0d6b2a" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "30dd8f400335e9c688e13cc0b1007bd21736a6d395d152e2" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "28899601fa95f532b030f11bbeb87011" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "35625638589bb7f6ccdb0222" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "cb8f672b04d706d7d4125d6830fff5d2ec069569bea050ce" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "375d4134e8649367f4db9bdb07aa8594" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "70610bf329683e15ecf8c79f" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "bf71e5b1cd6eb363ecd89a4958675a1166c10749e1ff1f44" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "9f502fb5ac90ff5f5616dd1fa837387d" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "a4b5138122e1209d" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "5b9d1dfb2303b66848e363793bdca0e5ada8599cb2c09e24" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "2ee96384dd29f8a4c4a6102549a026ab" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "3b33a10189338c3b" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "a35ae271f70ebacb28173b37b921f5abcad1712a1cf5d5db" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "8d97f354564d8185b57f7727626850a0" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "813d2f98a760130c" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "9bdd0cb826d5d28c2ab9777d5a0c1558e7c8227c53ed4c4f" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "daf13501a47ee73c0197d8b774eec399" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "a6d108c0" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "81b4d5ee4e1cbee1d8966fb3946409e6e64319a4b83231f5" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "bc2f9320d6b62eea29ebc9cf7fc9f04a" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "a47cdadd" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "5813627d26d568dfe5a0f8184cf561fe455eb98b98841fe0" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "817199254a912880405c9729d75ed391" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "d81d9b41" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "94f160e2325da2330fbe4e15910d33c2014f01ace58e5b24" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "80a1b99750980bf2be84a17032fc2721" );
            add_len = unhexify( add_str, "066fdd980cf043a732403ee5f65c82ca81e3fc858ad3cfa343014a8426fd3806770f127e2041efb42e31506ce83390ac5d76de2fe1806df24ce6e4bb894972a107ef99e51e4acfb0e325ab053f9824514b5941ab1ec598fbb57a5d18ed34d72992a19215d914e34ad1a22326e493d1ff2da7bc271c96ad3ab66d0c32bd711293" );
            unhexify( tag_str, "dd153cfd7aa946280660c445f586fa28" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "4785846f7c0524e78f3eb137fd433e1808af64549af69183" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "5334476a5fa3fa50dcc4b12f8ac00b51" );
            add_len = unhexify( add_str, "e70f82d1e3361ac5a5c9a087e47984d5533ba296f9b7e4a192a4ab28a833cdbbd5cece3415cf6fbb2f8055560b5c31c98d83d139954e1c03a464739f1eb5ad982c4371cf20b8984bbd97d5f40b336f5e96df3d272b95f7547be15c3bc05b3caac7d08c5eb5de8bdd246e74f6caa6bff76ea0417730ce72b911867f88fdcf73a0" );
            unhexify( tag_str, "c59231ddaae98e0e8db6b3fe8f4d3427" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "49b085fe1a8e1ae769ed09fc585d29eb24d589689992e6c5" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "899878b0684fb865d30190821817b88c" );
            add_len = unhexify( add_str, "f789eafe3d02826b619ca4fbca7bb1919e5c6f7c33824a2f7f815dc50e329979705f7ef61e9adf7899d34f1b8840384ff62ef6d29eea38c45d12be9249aca69a02222cd744d81958c6816304ff0d81d6714a2023b3dd9d940db5c50afd89c52774d28d6afde2b6c68425b6acbe34682531a2e57e2b9a7729b3e8d96a729b15cc" );
            unhexify( tag_str, "2c84bf7a8947ab93b10ae408243b4993" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "75847588760ecb6ca548747b743914c89fea367a5ccb81b6" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "7d8a9fd254e2061c01e39eb574951924" );
            add_len = unhexify( add_str, "b03c57dfd49152401a225357f1d6e533f3a423e5cfce07b8ae7ca9daf68645e5bd67b3ca2421eac447530b27c6dc6bd9c7f1b22441b8cc8c4ac26cec2c9c0d665a35b66d779a3772d714f802d6b6272984808d0740344b6abdb63e626ef4e1ab0469da521c7908b2c95a0fd07437c0e9d4d2451ae189ad61ff19f4efb405127c" );
            unhexify( tag_str, "e8aac14b53cdbc2028d330fc8d92a7" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "e3a18a96d2e45d2f60780dc39cee7160e28cb810bf09858c" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "26a4d659665ded39b7a1583de756d0ad" );
            add_len = unhexify( add_str, "83f8d9c58169b4c68032321197077ff5c8ee4ebb732b040748e1b55dcf53375ae86fb9646a672b5c5bc805a92c475cbb6d0ed689a58abdf2230250a7d3fbd8cfab07835fa85e738a7f74bc3e93616d844b1ec61b79f23dfea62e1815f295d43f61d7b5956103b31ca88afb0b3d37eb42cf77232dbf2258065232971c397dcbcb" );
            unhexify( tag_str, "dc034564d4be7de243ff059b5f9160" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "7be3909170ea7a2ff76f9f28241d8cc48ddeafa8517c6f8c" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "8dee7e29350c60c5bcfec89da6617d2e" );
            add_len = unhexify( add_str, "f6e9e7a7f9716760eb43060d5c80236a0f118b0f750ebd5df01fd2dba95c556ecd2e54a3f337767321abf569c8137a8e48c5b44037ba62951e9f9f709e6e4540a36d769f3945d01a20a2ed1891c415a16d95cab7ddf9bcebf18842c830067509a2a5d49a9684324c433d53824d2f8fd326b149af17f40e5bf5e49185738fba60" );
            unhexify( tag_str, "942b52277e9dc0a30d737d00f5e597" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "1fe413bafc4753e1511b580c830449bee56e0e5b9acb852c" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "e30829f64f3eda13bfb2ac572aceb3de" );
            add_len = unhexify( add_str, "6c772d08b4d7507e35804572fa697c646c77301954cc5c160941e49e230697ed8c23338b9f30c3ead69b1c1a2329ff025dcd3c0d0a9cc83fee4979448aa71ddb9d569bedc8c497a2a4ac3b60d087d7872f0a110bf90493ae7da03b0953734223156cd2d6c562e4a978a6dd5cdb229dd58dd4d0f50ac015f2f5e89dac4aa29a19" );
            unhexify( tag_str, "87737873b82586bb29b406946cae" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "b4bc4378d423931f9b320bb57df584c641406c1daa7448ad" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "eca70e10c0358838a3f4a45c4b016ccd" );
            add_len = unhexify( add_str, "68d1c045c1604e3c3dd4f7c7543240aca8dbc5266dc18c5a8071e8b09e3700b7cf819044b2722d8db92021f42a0afb295d7b16ecf4e4704a50a527a2e72d7f53617c358e3b7be3d7fecda612ce6842fcfaa68f2d1b8a59d8b8391779f2fab99f820862c94029f444abe62367c5de0a4becc359660e4a5366f7d482bdc362b866" );
            unhexify( tag_str, "06f95ca69c222a8985887925b15e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "1cd4414ffd24e830e2dc49727efa592e430a6a75391cf111" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "a08e32ad7d63f975de314ad2c0fa13fc" );
            add_len = unhexify( add_str, "20a271f1f4c6bea8f1584ab39a7179ec448650e2ff67a7338d1bc9fab7f73b2ce5222cd07ded947d135d9d0670dc368f0a4b50ece85cbf641877f9fe0ac6a7e6afb32fdb1b3cd35360bb80cfffc34cfb94dbcbee9ca5be98a0ca846394a135860fba57c6f0125dcb9fb8b61be681ada31a997638ee172525c03dd13171534a91" );
            unhexify( tag_str, "c68842cafc50070799f7c8acd62a" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "9e0ef9ed5e6f00a721a9893e1f0d9079c5aa667a4cdd2a52" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "5f015fd556e87ff0d0df586fb452306d" );
            add_len = unhexify( add_str, "b82986135e49e03f6f8f3ce4048ded2e63ee0c31ddc84929e022ee8561159179b3bb4403ebdafdf6beae51ac5bf4abed4dbc251433417ece3228b260eca5134e5390cba49a0b6fcbbbabb085378374e4e671d9ba265298e9864bfce256884247c36f9bddceb79b6a3e700cb3dd40088ba7bb6ab6aa11b6be261a7e5348f4a7d1" );
            unhexify( tag_str, "ec9a79a88a164e1a6253d8312e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "9bc8f15d98e089d60d4db00808700053f78b33c31652c3e4" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "5cc0ff9bb7d5b9b2aa06f6ecf669d5bb" );
            add_len = unhexify( add_str, "24ac95a6ed2f78853f9ab20f53de47e7f662f72aea454141e2131aace7ed2daeb395bbccdbf004e23ce04ad85909f30151b6526c1ce7934726f99997bbab27055b379e5e43b80ad546e2d1655d1adad4cbe51282643bb4df086deb1b48c1bd3ac3b53c4a406be2687174028ecf7e7976e5c7a11c9a3827813ade32baef9f15ec" );
            unhexify( tag_str, "9779b7c3ece6c23d5813e243ec" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "19afc43a4481f796d77561f80b5b2e1514c96c5d1d86e64c" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "d4c06595fefd4a81bbbd4b40c2e1989d" );
            add_len = unhexify( add_str, "98fcca51352998d0126b5539e3fb9a238ac31c05954fc206d381909aee70983b6ab99d3f3efe8530a1c3cfe3b62756321b1d0771a5940055eba1e71fa64f29291aa5e5b0af0fcc8e6f5a02688d9e93417225eded791a35217822ffb346d3fa2809b65abe729448316be30cf661137d3c0e49846cb0df598d90eda545afb64a5e" );
            unhexify( tag_str, "ca82448429106009094c21d70b" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "b4fc31dcfef6203fdb296cc928c13b7df56bfe6f32583057" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "6308a78dc8f3c90442dc52196649c38e" );
            add_len = unhexify( add_str, "2567d80c253b080c0158102558551445d8ce4d5ddee2014a2be5cbad62e1717a0fd4d2059447c3151192951eb11a4a7b19a952f6ba261c87f10f4c9032028de3cc5a2a573a4e993a690fc8954daa3ec92743e7343e75b646c4fa9cbc3fceb4f5d59bb439c23754c4d9666fbc16c90c0cac91679b6ad1bfe5dcf6bd1a8a67c6b5" );
            unhexify( tag_str, "9d1603799e2485a03e7b05a0" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "1c2d9412486c381440213e1588b6bb58b0da53300b9d3089" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "727ed8846daab874d5a9918b47d016f4" );
            add_len = unhexify( add_str, "656430f0c1423018b5e2efbb1e32a5385c1a9a1779c4dbd585dea91edc39ea8752ebfc2d8064251a8a5ae71e1845f24a7e42c6371c2ecb31e2229d5f4923bffc21d4804575a84836f3cf90ec6047bb360b558a41a975ece111b5284dfa2441705a6df54fc66ca6cc1af9163ecc46902fac337d5f67f563fde8e8e7e64b8588b7" );
            unhexify( tag_str, "05ee6ce13711535864674a5b" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "abf7a97569427225a4bd5143c716a22e62f84c145bb51511" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "e255088cdfe8ae5c9fea86d74d2f1b7d" );
            add_len = unhexify( add_str, "b850993300f54d078f83ceb9aef7345bbf758f92365b6625c210f61dad4f2a2319f51d883a383a706392d3dfca1706eba585a6fac8bd4294c0bb2cb3f6b454d5c97819e8e5c926754840261b07ec4ef1f87cf281d75c187839689944230306e1903047915e086043990745864819ad713d34a244aa4e9d755fdb137105d7eed8" );
            unhexify( tag_str, "0c9c17388d0610f99d0a093f" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "45a6df655e88bc880acff41520aafd0cc8aa8aeb8952fd06" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "1125e1de94970c9e7be70e58e7626ef4" );
            add_len = unhexify( add_str, "fe9838a445b8edef19b3e9f33c8c0c265b3a12c97b8ec57ceb94f65ae5227177de38f1e338dccb2b24e5bd0f0eb8127f83eba0f1ddfa55198789df0cdd1d977fcb985ad9c7d51b96e749d2cf3cc7a1ec4dfcbc641a1a022d55def328e081af890a7e699f2dbafdf506389e045aa1219239d5868ba675a3925602b6fb6f6e6d37" );
            unhexify( tag_str, "1c3bd1e0d4918e36" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "279f4f2ab4b70778fdb9ca7800cd20e323601d7aa2c75366" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "0f7b402560735cf03d5da58de5b6c685" );
            add_len = unhexify( add_str, "7dd9a8c848bbcf5127161c8a419a436a0dad559f7c1613cdf41594e177016acb1ccf44be852185c42e7120902a42efe83855995ab52cf5c190d499fcfd698c671fd72949dc3ea7ddb874e586a3aa455a021cec7b5f8608462ca66f926aba76e60a5846d4eb204155cd3c1328da51ba35c3007b8bb394f34e3a8b81ddd2ea1115" );
            unhexify( tag_str, "dab612351f75e2cb" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "6716ab937755684af7403e6fba5452c1b11568a9047bb50f" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "2fd5a446dd564619ef75b6e00905ffe0" );
            add_len = unhexify( add_str, "20d261d3192996c21da69e979c26f5f937e6ea4cb7b05c6ef556ce4d86ca0fe85ec2425d274c43b5212fe9d27bb48b04e887461a9f45f524059b87eaea2e287a8d4537f338b0212012a9d4b6610e8c97dd554e0b3c3133e05c14d0ddab3524c93fd527e223b1996b4cff0a4a7438f1d54890bf573cd803941b69e5fc6212c5d2" );
            unhexify( tag_str, "f1d743b7e1b73af5" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "7dc94b5bbd6315ad8d2b67f0c683d10cf456f822a3ebb024" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "6f3eedeb57dcf12bfb3cd80849893c90" );
            add_len = unhexify( add_str, "ee1ff367f4b23c156e3dccff84ae4bf2b8ecec1fb5ffd25ccaa93b6c6834389bd79655bd4bac75238eb0f65d3603ecc57c8774798309e85b6677e78ed2077b712cf28795d0dc8fee994f97373a82338ef67c62378136a79a990ecbcd6367445e805efa98f9168826e57cb8dd7e7b1d5c89ad98358646fa56dd2a71c40e0275a1" );
            unhexify( tag_str, "4dc74971" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "3bbe223e253bf272599e28af6861013ecd0c88710947ed41" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "4fbf09ffaffb600f0de38fb12315cab5" );
            add_len = unhexify( add_str, "5388146f6479f7b3b280f45655a95b847ee27c734fb2fd91f6c009b1ab1810c772c7435d3221069f9490d251b76e740147906ac1db1c209c175b21aa10881c44fb307d4d2900aa3b1d56fb0edb9f2a58505653a17fee350e12755b9656bc65c78c1593d5cb7178e29f82209caf53e60fddf725f6957cc9718bf410c4a0229ed4" );
            unhexify( tag_str, "fb845ab7" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "461877813acfe6e9979eab729b52e3d192b3236758bb6563" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "6985cf77b75a47a3978dd6412d59200b" );
            add_len = unhexify( add_str, "385551854a89ab37063ba0ed911501b3d632153c5c2992e154c0a334bc36620476f11495437b842409e0954f7352cbf288d158bdbbaf72621ea2ce75b708bc276f796c5aa7fd0071e522c5f175a9e7787deef79f6362101aa3607b4588f2e1df7127f617c6073593a1c792b959e201e4a7a43ea8b1c3af026376439ef629266c" );
            unhexify( tag_str, "c840d994" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "09770f9114120a2c1c3cc416fe0eb8699e07141158a5bdff" );
            pt_len = unhexify( src_str, "875e2e5b5c02e0a33e71b678aa29c15ce18ec259cf4b41874893ed3112daa56ff2a7475681b8b3d9028ef184d30658e881c908f3588f69899962074db4ddfc0597f8debb66c8388a1bccf0ffe2cf9f078dc1c93f8191f920754442ad4a325985c62de1a57a25de4e9ed5c2fd0f2c8af33f3b140bac12bf60fdb33e0ec557955b" );
            iv_len = unhexify( iv_str, "cff291d2364fc06a3a89e867b0e67e56" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "81f1eb568d0af29680518df7378ba3e8" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "4fbf1c785c087ad06b43d4163cf9b9396deffd3712856379" );
            pt_len = unhexify( src_str, "96a690e5319c94d94923988025307e543f16fd970aec24524cf9808dc62b093359287251503f4231bf52cd1a16a80bfa82d8f585d96855dc1932f4919a92da2618d6448fc18a234f9acb386ab4ab4a9e38ea341e7c54faceff38c162d74e7fabbca13aadb71e9c8ae6072e7bef4073cf08aa7faaa6d639f98d15bad4ed183ced" );
            iv_len = unhexify( iv_str, "1c8f41424acaf009996ceaa815b24ad4" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "9f3c0349c5a4a740a82d6d63bf00fb17" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "6100b091e52366fb422251d9b68974b6c666a62a8bb77a1ffd7c7d1ae586a6ee763b84dc11aace02a25af91d194b70b3265ec46872fded54275b7ddb26ee1f20c857328f46a694fb1dce68bcaecbd587ece5b505d658d57d50333e30b639eea1f6537b37c175f62497c6c84e3cfddae214285d2d68d90dd5cd8ce2273d25c8ca" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "6100b091e52366fb422251d9b68974b6c666a62a8bb77a1ffd7c7d1ae586a6ee763b84dc11aace02a25af91d194b70b3265ec46872fded54275b7ddb26ee1f20c857328f46a694fb1dce68bcaecbd587ece5b505d658d57d50333e30b639eea1f6537b37c175f62497c6c84e3cfddae214285d2d68d90dd5cd8ce2273d25c8ca" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "3e0ce4fb4fe4bb2fdf97b23084ff5671b9b899624184acef" );
            pt_len = unhexify( src_str, "df89974b1534f0ba262bbea5efe39d8b72820cc8a720cc99520fedbf667515c3f6d8c3e25c72c48c1cff042171df58421741aacb2a49f23167257be7d7004d56b14901b2075eaca85946e9fbf1bbf4ae98227efc62bf255a25dd0402d37c67ba553531c699dd89ff797e7a5b5b9a9aa51e73ca2dacfda0f814152aa8ed8c79f9" );
            iv_len = unhexify( iv_str, "a950ab0dd84115e3829ab0ad3bbb1193" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "25cfde73e7a29115828dfe1617f8b53e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "847b54e176ccc83081cb966efc4b4a3bf7809ce0b4885009f620f61fafcaa78feee91a835ae6c1a942571811108b1e81b4c4ddac46aaff599c14988c9a1fb9f387ab7f1357b581568b7b34e167ac2c8c2b2b8a4df3fd7ad8947a363c1c0cb782ec54b1901e928821cf319669dd77eb37b15c67f13ad787ff74312812731ca3e6" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "847b54e176ccc83081cb966efc4b4a3bf7809ce0b4885009f620f61fafcaa78feee91a835ae6c1a942571811108b1e81b4c4ddac46aaff599c14988c9a1fb9f387ab7f1357b581568b7b34e167ac2c8c2b2b8a4df3fd7ad8947a363c1c0cb782ec54b1901e928821cf319669dd77eb37b15c67f13ad787ff74312812731ca3e6" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "6be3c66b20e5e66ababbfba1b38e5a716eafce23a1767b69" );
            pt_len = unhexify( src_str, "de1cd978354a499415176f260021abe0a8c5bc34d166f53d20e02e413e1377ce4ef5d7f58337c62251a3b4ddea0dea23c40e5de037fd5dd8a558eb53bffa4e8ce94899afa8284afab503c1a485999a154d23777f9d8a031b7ad5c6d23d6abbe3b775c77876ad50f6bed14ac0b2b88fb19c438e4b7eb03f7d4d3fcca90dd01260" );
            iv_len = unhexify( iv_str, "3a2acf69bba19f5d1d1947af2cfda781" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "f826d212f7c1212fb8a8bf23996826" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "fd1f7b56e5664cf4c91e58f7c50f6c5e98e42ca2e4adcc00348cee6f662b382ad4022da54a47d8faeb9b76a24dfc4f493c27fc0bc421a4648fad7b14b0df95d8752013feb033b1fd971daa2c9a5df898bece6a3b8fa078dd130071df20a68cd0f394be25dcbb3e85bdfa0df4797fa6f01f5f0da7a6e86320207ddb5b3be53ae0" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "fd1f7b56e5664cf4c91e58f7c50f6c5e98e42ca2e4adcc00348cee6f662b382ad4022da54a47d8faeb9b76a24dfc4f493c27fc0bc421a4648fad7b14b0df95d8752013feb033b1fd971daa2c9a5df898bece6a3b8fa078dd130071df20a68cd0f394be25dcbb3e85bdfa0df4797fa6f01f5f0da7a6e86320207ddb5b3be53ae0" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "d16abb9f5b38d7f5abba9dc36995ce6ce928ed822a07b7c4" );
            pt_len = unhexify( src_str, "e72f29b1fc1dbfc2d93a0f3b79ea4b9806ce9b2c4d490ac5c0c3c793df9dc7df5471e834b84d18afa5a7516f9a6a813a9b65ae2f083a854730547e28a1f60fe97d8dba1d2d433e11847b9bffd8873ec634e64365530c905dd6f274e45c9795ac127a6f356f63cc6c116c5dd8c628e7e17e1fadc58f8452bf21f53c4133198118" );
            iv_len = unhexify( iv_str, "3cd95429c6de1d327b9eb3c45424a87c" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "13521236f190f78e75c0897c5fb237" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "cd8bb97c28df092b6783ef653fd26f2bdc27c442bab0a4c7bee2789f389dcd1b280c0231672721bfbbc939a0449557678ec61ba0afb2e5817e6f7d94387f84ecafbfa1216d65e7f5025f47b0d2905cff7c99adf8306a3d9850c5908be05f87cb1d36a4837dba428aac97d7fbc18e3778f8d81a319259504c87fc94bd0766ed93" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "cd8bb97c28df092b6783ef653fd26f2bdc27c442bab0a4c7bee2789f389dcd1b280c0231672721bfbbc939a0449557678ec61ba0afb2e5817e6f7d94387f84ecafbfa1216d65e7f5025f47b0d2905cff7c99adf8306a3d9850c5908be05f87cb1d36a4837dba428aac97d7fbc18e3778f8d81a319259504c87fc94bd0766ed93" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "0bc344b1a4078807e5f53a6e7e1e36fa83108473ae2fb4c2" );
            pt_len = unhexify( src_str, "8bd73f94c71e3765bc7d17fdc90a9ba6aff9648b46300e4048985fbbd7c60c39c3766f7c524780bfc2296dc11e1132134921760a373104edc376eab6e91e9a60a5c4a5972935df12eadae074722bdc0147c3caf6a62fd449ef37d76b65f6d210283c94ac524cf13186e444d80a70b01e4373cc0462546f1caee6b49e738a742c" );
            iv_len = unhexify( iv_str, "bd505fcba464e6e2c58fdf29f5695fb9" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "8510fff71bb879f56ea2fe43f6ff50" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "c8097398fc21f93eea6a95aa93a3231096817b65520bc549" );
            pt_len = unhexify( src_str, "80b0abbaebbd537a0810ed75cd172d29d50f5982e4d01f8664ddb2dfda8f57fa0ed87e64a779a1d7f5e568b6acfdc739572a7176752307b430fb1fa1c3c2c346477cebe7d01b16745ca6c8929a7f446c03ad9a9e8a5a935de78ca6c701e8c1c5e6d2550c42949cf5342fb5ef4c6ab9bb02ace8388b16edf72a1237e5d1d0e820" );
            iv_len = unhexify( iv_str, "776248381941e16908f52d19207881f5" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "7fc4388b2f8eab0f0c2d6a08527e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "76d4bb5694faaf344db83bc6d6c47d56bb6ab52700826f2d" );
            pt_len = unhexify( src_str, "9e31fda6a171f0d4a5f2af2c4f827b1312d9dda5d78fa329b8f1b6373b9b29be358601e5bb0d0c615aef4b9e441c811219f1f2ff2d0ab23e0cd829a88b5b615ee72e5e3ea604fa26cc6438ec4c30e90f7348e9116adf8e8efb7498320d2da16679fa546b1aa9afc7720b074c4e48e06862d41428c9e71a4772c2e195a6f36978" );
            iv_len = unhexify( iv_str, "603977845d82faccb401817ecce6e2fe" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "c955a3bc316841be07e406d289c8" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "a3e5020695587984074d78d9c98b8e1a5719e5f88372740e" );
            pt_len = unhexify( src_str, "c0bfe3b2dc4dad17ec5a7662d86847fb67e582cc0baf469bc9baa7a075d48a8b97521a1072c2798bfbdae5ca3752eda1cb96fe5cf24af989eb77a2948aae3d8b70d83d93f84c49347f788480f34051621c358c03cf8159a70fc72cb8bc02876234ffe76b181da8b22b8796c87b0904da1af46de519c20d8d1b1dc7cc24e39ba5" );
            iv_len = unhexify( iv_str, "4cd56de54e5140a587be7dfd02d3a39e" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "1a29527a41330259f918d99d7509" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "afe986ead799727063958e2ce13ca846f76c51605439f839" );
            pt_len = unhexify( src_str, "7c1b354a5bb214bd95147e32d81e658705089c38035d0ea423eb1a5c82f97443c6903d2cf1ba7a007eec7c8ff98b8f82b073d9636a79bd47c7f2f639a8eb4e92076f9ed615766f43ac3a4f1687301ed7d507766605e0e332880ae740ab72e861a2cb6dce1df1ff8be1873d25845ee7c665e712c5bbe029a1788634bce122836c" );
            iv_len = unhexify( iv_str, "f85a95ed10b69623162ab68d1098de94" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "3cf1cdb4a4fdc48da78a8b4e81" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "a7f252ad7983e7083260598051bffd83f40f4d4a8b580cc2388d720a0979dde71549ddcb86b0a62c4964fca591d0982f3a203f2f8884ff4991f17e20f759ea7125ba2bb4d993722f23938994eb2709c850f33ed9889e5a3966f9d7b76add46aedf230e8f417425f9db79ccd46b5660361de7c5d87f71a9d82c491c0c3daaf56c" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "a7f252ad7983e7083260598051bffd83f40f4d4a8b580cc2388d720a0979dde71549ddcb86b0a62c4964fca591d0982f3a203f2f8884ff4991f17e20f759ea7125ba2bb4d993722f23938994eb2709c850f33ed9889e5a3966f9d7b76add46aedf230e8f417425f9db79ccd46b5660361de7c5d87f71a9d82c491c0c3daaf56c" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "2cfaa215841826a977ae6adfdd993346210c49dd04d5d493" );
            pt_len = unhexify( src_str, "e8eb3b6edd0ca4201b49a6a83036445aba1a1db040f3e74511363bce769760a9914e05a067f555ca15a57c6e02e66fbe4e04dd8c8db8d6d14ebc01cc7d84a20ff0aacb69bb3679d6b7d9d2e07deda7c2d4fe4c584fe1166e78d21dc56b9cdad93709c03b9145b887f87b4f605f24f989d5e0534fc71a58e8a8619ee99f69e5f5" );
            iv_len = unhexify( iv_str, "537a4ee307af3072e745570aaaadce34" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "df01cffbd3978850e07328e6b8" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "128ddc83d2170c403a517615056dceec0d19d6fd7632e738" );
            pt_len = unhexify( src_str, "cfe9f7797ee37bfc4f564419bf2268c964479efa7435970874154432930f3b2736438da4dc9c76200009651340e23044bc9d200a32acfd4df2e1b98b0bae3e9ff9d6e8181d926d2d03f89768edc35b963d341931ac57d2739b270ce254f042b64ceac4b75223b233602c9a4bdc925967b051440c28805d816abe76fc9d593f5a" );
            iv_len = unhexify( iv_str, "5124b410c43d875eca6ce298c45994a7" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "56ad9c1653f11a41fd649cccd8" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "cf91f087fd7faf362caacf4a68cff51ec57b3075563e4ad0955df20b366e92bd75c3762cf4a6f0eb859872667a5c55aa5d94f5ac9479b1b9c9345b50f82379d551506a2ab02b0441b14b28b78a12b38500d703a8c19888fe612d4710eec7cd18c16d6a4b55d3c69760e2bed99efc8b551dbe2ac9b9b64715f87180b8e14d1795" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "cf91f087fd7faf362caacf4a68cff51ec57b3075563e4ad0955df20b366e92bd75c3762cf4a6f0eb859872667a5c55aa5d94f5ac9479b1b9c9345b50f82379d551506a2ab02b0441b14b28b78a12b38500d703a8c19888fe612d4710eec7cd18c16d6a4b55d3c69760e2bed99efc8b551dbe2ac9b9b64715f87180b8e14d1795" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "98581c28983c4da321ce0c419cc0d476d539e77da513c894" );
            pt_len = unhexify( src_str, "bdef5b65b5111b29e781a6b71a0160179c52b5bccb1ac5c0377b26cf3f61432f3ccd67633a836357c24b5099db0510a7f8110f59e8227cacd11f17ea1798b5d4d68902ca6c6eccd319fef14545edd135078b38d43b61c9af269fc72f7a209ba7897e4c6dbd21bb71d7e93d2d2426ffa1557cae28e74059d3baf06ba419a47b39" );
            iv_len = unhexify( iv_str, "ff10234524433b871202c2cca6acb194" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "984943355a7aef15c4fb8033" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "808e28bfd441cb8890416a757d252c986daa8d607ac9cadd2f4fd29eddbcf3b859ba298e14a4ccefe2c2752b123f87b98d6708fde48faca4bc7dd818a7ea76cfa4357932e59cb6be0e9283bdfb49454b86b9fd04aa8cdef503c65d13fcff42e9cd8f142f8c06cf7daa6d8ef8b9c9d69c39e8afd980048fecf731fd674b2a814b" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "808e28bfd441cb8890416a757d252c986daa8d607ac9cadd2f4fd29eddbcf3b859ba298e14a4ccefe2c2752b123f87b98d6708fde48faca4bc7dd818a7ea76cfa4357932e59cb6be0e9283bdfb49454b86b9fd04aa8cdef503c65d13fcff42e9cd8f142f8c06cf7daa6d8ef8b9c9d69c39e8afd980048fecf731fd674b2a814b" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "167b8b6df8014c8f3de912b77f5a0c113580aa42d785298f" );
            pt_len = unhexify( src_str, "4f787de12ba907a589edf74c8e7a6cdaaabebddd465a86e170e1efc289240298b516fddc43c7fd9bb1c51720a4455db4dd630b59aebaa82bd578eb3cb19f8b23ee6897c1fefaef820430efa6eb7d6ff04de4d8b079605fb520b0d33e96c28f0cd71983c4ce76c0ea62fd7209d21ec7b416881d545824a73d1f9f8d3323fdb90c" );
            iv_len = unhexify( iv_str, "49da91e926091a448d57d521cc90f3c0" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "99198f55f9fa763651bba58e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "71f5f8505fba62f08fa0557dd5407fc83a852c6007ccecc8" );
            pt_len = unhexify( src_str, "3e19ec02365e450e946123a3362f9859352eb52902a6bcb8a782285dfac9d2b282f56302b60d6e9f53fddd16bbf04976cf4eb84ef3b6583e9dc2f805276a7b7340dec7abde4916fb94b0ed9c9af6d4917b27e44d25f3952d0444cd32a4a574e165a23fa8c93229ceb48345171a4f20d610b5be7d9e40dcf7209128f029fed6bf" );
            iv_len = unhexify( iv_str, "b5efb9feae3de41b5ce9aa75583b8d21" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "9604d031fa43dcd0853e641c" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "4cdb38f8185a4186fc983e58a776a6454b92ecf0bffefe98" );
            pt_len = unhexify( src_str, "1ca72c50a093076e9a9dfa09888b9c89eb36a942072fc536a81713f05a2669b39fdb2871b82ca47dcaf18393ca81dcb499aafcc4ed57ea79f8d4f9bd63540610215b2c65481b294638cec41264a7fdca4230df5fe1e7e3d8d26dcd0c435fec8e9bf778f9e6f13482157a9722761601e08425f6160d3bb626ae39ee1117b0353c" );
            iv_len = unhexify( iv_str, "aef257dd44d14d0bc75f9311ef24e85a" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "d951becb0d55f9fb" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "2eaa7e922dbd8963e2078aae216636276f3f7cb5d7f35fa759e91bddb6e247a93c388241ba1d0d37040c0b9e447c67d35b4991c1acce97914f3bc22ee50171bc5922299983ee70af79303265bc1ae1e7334202460618b4a8891d1a7eaaac5cac1e4dce024ce662d14849993f89e771fb873644b552120fd346250df39aaaa403" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "2eaa7e922dbd8963e2078aae216636276f3f7cb5d7f35fa759e91bddb6e247a93c388241ba1d0d37040c0b9e447c67d35b4991c1acce97914f3bc22ee50171bc5922299983ee70af79303265bc1ae1e7334202460618b4a8891d1a7eaaac5cac1e4dce024ce662d14849993f89e771fb873644b552120fd346250df39aaaa403" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "ee8d3aced3aa3cb2166aa66c4a252c12dc0978830d0bc75b" );
            pt_len = unhexify( src_str, "ee69b2421d43a9f383d99f9802ba4d6cf1c537b42041c86cce681049bb475e5098d4181f1902b0a49c202bf34ef70ea7b787fa685ab8f824fcc27282146d8158925bfef47ccba89aa81c0565eacb087b46b8706c9f886b7edf863701003051d6fb57e45e61d33412591ec818d016eec7dee4254636615a43dacb4f1e6ec35702" );
            iv_len = unhexify( iv_str, "c15c9c0b0b70c7321df044bfde2b15fb" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "c5c9851a6bf686d0" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "4a8538d609444e3197ab740cd33b66db1cf53600096b94e0" );
            pt_len = unhexify( src_str, "8c2b8fb775d1b21c41a3dcf48ad6d68ab05be3879f9b94b305a6ce4d799e3a992c1c3a65a3e4eab563edb57424927c90c76e49386e29dd5e7de2800fcc0eefbc8b4f977f71be3754c006ee93dc09b1cfa59c424b6b3987aeb56feefc21004c63e8284b6845e395bc8843cca0917267fb4a8f2db1f7daafe7a9da95083a44de70" );
            iv_len = unhexify( iv_str, "0bd64d222532dae8ab63dc299355bf2a" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "3477cad1fd4098b2" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "447f0f065771b6129952e52206a64fe0844658ed685e39cd" );
            pt_len = unhexify( src_str, "fea5d227869e527882c63a68a6623f4a699df82b3dc715c7260a5554336df8376744c05ae89ec27d40da02d9f1c5e9e29405579fd4132143cb21cdbe3edfaaab62128ecc28018725c8dd309d2376223d2e2edfea9765699b2630ff5d9fe9bec416c0ca6418b938d195d31a08e4034c49d79e3a249edd65f985230b33c444dd02" );
            iv_len = unhexify( iv_str, "37e3a300542d9caf3975c6429cb8a2e8" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "06bfca29" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "e1bdd1c212b159b87e41a5f64dcba6b27aa0f5c8871fabfb588df0e06bd7730ec1beb0e3388f96c992a573ff69b34870f83c53fb65b420c1c6f92e2aa6f03917e8203d77c7f5ee08baf9fab12f9d38fc0ffb83807ba781c3dd7b62edca2121f68ef230b42b8adbd4cea072209d02713789ed559b83739a54cfde69e68bdc4128" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "e1bdd1c212b159b87e41a5f64dcba6b27aa0f5c8871fabfb588df0e06bd7730ec1beb0e3388f96c992a573ff69b34870f83c53fb65b420c1c6f92e2aa6f03917e8203d77c7f5ee08baf9fab12f9d38fc0ffb83807ba781c3dd7b62edca2121f68ef230b42b8adbd4cea072209d02713789ed559b83739a54cfde69e68bdc4128" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "f465e95f6fc19fe6968b98319b547104d0c01c17105f8fc0" );
            pt_len = unhexify( src_str, "2426f108368a00d2a49670a3b64b4f0569c6da9660163e7b209ec3f8d058ee11f7818a8c5030c5f4ce6e1e5a93faa3e5ae3d0bd5d712fbc891cfeb20845707edcf5e29719a5246a3b024fb12d37bd1b81df3812fd50b1dfb3e948ce546dd165cc77f903c07fe32bc7da7fbc25036679017317ce94cd8a00c1bce7379774f1714" );
            iv_len = unhexify( iv_str, "6cba4efc8d4840aa044a92d03d6b4d69" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "92750ac9" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "2e59b104c1a6f6d651000396adbfa009bf4cf8cbf714da8e4d3b4a62bd7f522d614decf090c7552a4b9e8d7ee457ba642d5100c0c81c14cbba8c8ff49b12827f6ebd41504ccb6dfc97cdf8532d1f7f7e603c609efa72d2ae0dce036ec4ab36849a0c06f8737d9710075a1daaed3867ca0a7e22111c0e7afae91f553b6fd66c6e" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "2e59b104c1a6f6d651000396adbfa009bf4cf8cbf714da8e4d3b4a62bd7f522d614decf090c7552a4b9e8d7ee457ba642d5100c0c81c14cbba8c8ff49b12827f6ebd41504ccb6dfc97cdf8532d1f7f7e603c609efa72d2ae0dce036ec4ab36849a0c06f8737d9710075a1daaed3867ca0a7e22111c0e7afae91f553b6fd66c6e" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "f08e3e9f7b3a20ccdc4d98b56f2b567399a28a6b3908deab" );
            pt_len = unhexify( src_str, "a986e816f1eafb532c716a555cca1839a1b0523410134ea0426ab309520b339fc1fdeb40478ae76823cee4e03b8d3450e6be92d5ff17b2f78400f0176e6d6a3930bd076a7a3c87c3397dcc0520c6b7b4ff9059ea21e71c91912a74aac2ca70eec422b507cc5c60860bb8baca01eec2a3003970ba84011efe576804b2820e306c" );
            iv_len = unhexify( iv_str, "4f4636d1b283bfa72c82809eb4f12519" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "16c80a62" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "87b5372571fb244648053c99405999130f87a7c178052297" );
            pt_len = unhexify( src_str, "ae078d1554fc6a14447a28c3dd753e790f7ef9b53e35c3e0fe63a7b1b326bc56034847f8a31c2d6358049aae990bfe7575b439db370aa515e225e0ec730488c700a7b0a96a7b8e4e8e4c6afec20decd16fe3c0f3f8d7a6cf7a8711d170829d14c706cceb00e133b8c65c8e08cd984b884662eddd2258ce629abf6b9dd28688c9" );
            iv_len = unhexify( iv_str, "a1cc81b87bd36affe3af50546e361c9e" );
            add_len = unhexify( add_str, "684ce23f59632308d7db14f7f6eddaf4d83271fb0c27401b09518a775b36252540f14305f0dae13ff6c0dc565c9e570759e070c8ac73dfb97abd3285689a7cdcfc941f6271be3b418740b42ba4a114421065a785be3dfa944c86af56da8209779e8736e62529c418b507c6d8ae002cbc0431747722afd64521734f99273de455" );
            unhexify( tag_str, "98177b3428e64bc98631375905c0100f" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "8be7df33a86b1162464af738de582a357d0ce8e213bba1b7913c0d13ad759d62c3bf4366f5130b3af2b255b7ad530b4977627f9e76b07e360c079d0f763dabbd22e976b98cd5495c6182f95bc963aad4b719446f49d3a448d11cac5bfcba4b675b8e4d88a389e2580e8f383f95bf85c72e698680d2a2bc993c9ee1ce0d1f1ac3" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "8be7df33a86b1162464af738de582a357d0ce8e213bba1b7913c0d13ad759d62c3bf4366f5130b3af2b255b7ad530b4977627f9e76b07e360c079d0f763dabbd22e976b98cd5495c6182f95bc963aad4b719446f49d3a448d11cac5bfcba4b675b8e4d88a389e2580e8f383f95bf85c72e698680d2a2bc993c9ee1ce0d1f1ac3" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "a2d069b826455d5e79e65db4f1d2b6a29ae9f401bc623917" );
            pt_len = unhexify( src_str, "acd6225dc5b9109d56ea565ab38dd4db432a7ec08f0db04f1c6b691c96d2eaaa6be62da7cc7fd75f931716c7f39705ea7cf828f1a5a325955e9b2c77e7fb2d562be6a89b3351b1b3d1355b43b73ed425049430314c16bf0836ed580e9390a3b8e2a652fddbfa939ca4c3c99765b09db7f30bf2ef88e1aa030e68958722cb0da3" );
            iv_len = unhexify( iv_str, "6d40a0c7813bc0410ff73f19bb5d89c9" );
            add_len = unhexify( add_str, "9960376b1898618d98c327c1761959d045488cc6198238bbe72662f276d47b41e8aebc06dbce63da5adcb302a61ade140c72b9cf9f6dfad6ecedd7401c9509fae349d3c7debe35117776227ba167f2b75921d7321d79f4ebca13d20af1638a1567043365f179f4162795fe4fd80b5d832e4ca70e7bf9830bc272b82182f70d2e" );
            unhexify( tag_str, "010195091d4e1684029e58439039d91e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "f3252351fe8e7c628c418c1a49709bf1f8e20add82539948" );
            pt_len = unhexify( src_str, "7e8d2816d280c91d232bad43b6610e2d0532a9f670f221a3a975fb16472c2e83b168115e87a487bcd14b37f075e1faa59c42515c353cdefc728ac617b7d273fa96778e3fb5f7a1132f8e2add4a57015b15d1984338b7862356243d1c5aa628406f4a507498eda12d2f652c55e8e58113ed828783b82505790654f036b610f89a" );
            iv_len = unhexify( iv_str, "eacd2b1c3cf01bf4ea7582d8ee2675d5" );
            add_len = unhexify( add_str, "141cb39a2fb8e735e0c97207f1b618a4b98f6b9bf8c44a1c8e9ea575a7759cc2a02301274553e7744408b2c577b4c8c2a00e18f8717fd8a6d2f46a44eeb05d685fbef7edeb4229e7ea9b8e419ffcb504d33583b3ae421c84caeca9f9789047dd7b1810318d3765307233567bc40e003401c9f4e1b07a2a7162889e1a092aedc1" );
            unhexify( tag_str, "63a310b4f43b421a863fb00fafd7eac4" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "699c146927ae29025e5b20088b20af27bc75449e4725ee6b7d5dc60b44ba8a06f7d265330c16060fbd6def244630d056c82676be2dc85d891c63d005804085c93ce88f3f57c2d2c0371c31027d0a4a0031e3f473cb373db63d4ff8f65be9ebe74045de813a4e6c688110d000f6b12406881c08085c9348e1f0315038907e33f7" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "699c146927ae29025e5b20088b20af27bc75449e4725ee6b7d5dc60b44ba8a06f7d265330c16060fbd6def244630d056c82676be2dc85d891c63d005804085c93ce88f3f57c2d2c0371c31027d0a4a0031e3f473cb373db63d4ff8f65be9ebe74045de813a4e6c688110d000f6b12406881c08085c9348e1f0315038907e33f7" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "e462957f2c500bf2d6bfa9af97938fdd8930e360ea4175e7" );
            pt_len = unhexify( src_str, "82a7a6dd82a5ea3d9a8e9541d854978487eda298b483df02b45c76b8b38bac98ffd969dd160a2765595b19d4ea3e64351ce95764a903f595dd673d13facf5a5594e01be1d60a0c6d28b866a1f93a63a74fecb6d73ac6fb26b20c008b93db53e9dc1d3e3902359fd47734fe22a5c6958f97e9001cc4e8b6484d9542dbbdfcfcdc" );
            iv_len = unhexify( iv_str, "b380584a3f4e0e59add4753c282f2cf7" );
            add_len = unhexify( add_str, "682b0af6592eef173e559407e7f56574c069251b92092570cbb7f5a2f05e88bed0af48dcda45b2930b1ee7d5da78dc43ec3598a38593df7c548058eda3c9275c1304489aff95f33a6cd79e724e8d12ca0ae92b20273eb3736efcd50dc49e803ad631dcbf64376a45a687eb4e417aef08a3f5f8230d3f0b266ea732c21ed2eed7" );
            unhexify( tag_str, "28a43253d8b37795433140641e9ffd" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "4a62ddd87f41c6df756e8da0985dcd8c91e73ba395b3d79b" );
            pt_len = unhexify( src_str, "37a83ee6dbdece212446739ea353cb957b9aa409c88bee042bbc3a6e5199aeb28f2b4b00ff433c0c68d6db5a197566019db8a4c7a792e2839a19a302ee02bee046adce04c1fbbd5b0c457d7cbe277992ce2c153d132269e2d1f12b084cf3026a202b4664bc9d11832e9b99c7cc5035dcfde5991dd41aeb4fbf8bec5126a9f524" );
            iv_len = unhexify( iv_str, "1d1843e2118772d76a0244a2c33c60bd" );
            add_len = unhexify( add_str, "028b92727b75b14cb8dfeb7a86a7fec50cd5de46aa4a34645754918b8606819d4bf8a2e7531a05ae5505492ca6cbc8c0e6d6ab2dea23bff1fdf581bb780b4a3312aa39639383fd10bcf92489801954733f16b021c2e84809345216f8f28a99773341e40c4a64305a2098eaa39f26a93bd556c97f02090e1a6c181a4e13e17d3a" );
            unhexify( tag_str, "ab738073228bdf1e8fd4430b5c7d79" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "e702f1bb9a1f395c74fca0ce9cdf29e7332c14acaca45200cd432a5767be38929ef8de43d0e1a5e7300c1eb669ac1ab997b31cb1403af8451e77e63505920af0f8c3abf5a9450ea47371039ba1cf2d65a14fa5f013b7ce1d175859404dcf6461a36e8bc260e7abf739d8951ddf1a3754e2d65e0aa31320a5ffca822023bc0906" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "e702f1bb9a1f395c74fca0ce9cdf29e7332c14acaca45200cd432a5767be38929ef8de43d0e1a5e7300c1eb669ac1ab997b31cb1403af8451e77e63505920af0f8c3abf5a9450ea47371039ba1cf2d65a14fa5f013b7ce1d175859404dcf6461a36e8bc260e7abf739d8951ddf1a3754e2d65e0aa31320a5ffca822023bc0906" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "fc46976d38a581a7042a94ea4b5bfe3587ddc65d1162d71e" );
            pt_len = unhexify( src_str, "4b9e858fc8f01903e426112192d4ae4686b1ae4d683b75afb2b8c63590275943d0d6d6a23b6d35796a2f101203acba107474ca6f4ff6dd87d6b77785ad1d160ef2755d84092dc70c86db5e639b689943b15efa646aff44b3f51f5d3f4cf6c8f7fc5adfe7bf2d72f75b93b8ee94ef3fa69ea0fc0bb77b3983901fdcd30bcd36f5" );
            iv_len = unhexify( iv_str, "b5e92563dd0339df00b7ffa2239d21bc" );
            add_len = unhexify( add_str, "7b6f6e104acbcd7188161477d8e425ff99add22df4d22de7f28d0a0075ca4ef848f68d07ed22d3165c08e40890ce04d1bd05b1a6ccb2fec8193d5f7dffc93d97a0c036b3748f708b011b68247a0249b9e1a60b652164e5c2fd7210377de804ac010c8aa08a11f40af97e8370a59f936cd14c22ea7a236d904145adc04a241fc0" );
            unhexify( tag_str, "d4356cb417953b01f7b1110c8aa3eb" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "403e49feadd4db763652ed5c4b1e12680cfe0abc30f4696d" );
            pt_len = unhexify( src_str, "221c61d769febce3913bfead9a201a805f11005ddcac185cbae00ce749de9c4362889b1b0d9546e91598e0ddedb88b673a90acca65d7e71a85636be052f361839a646dc8b834c02f3e2261d370e6bac9636b7536225b5ea77881200c8a3450d21bfd1e11afb3a470e178ecfe944a25a7cd0254e04a42b67723aac8afffd56fee" );
            iv_len = unhexify( iv_str, "1a60258a56e15f92814b4d372255a80d" );
            add_len = unhexify( add_str, "a4ffa9e3c612103224c86515dad4343cbca7a7daf277f5828670834f4d9af67b9a935c71b2130dfbc929c4409bffb7974ffa87523b58890770439c33342880b33319c626bf776c1c0aeb9c2a348a7681572f4ff711d94c192f3450e8b1275f9d02c742a2c9f1da316e9918bf787f22699172986cb9b10fc56d5f6b8392ff92b8" );
            unhexify( tag_str, "62646fc8bfe38b3ba6d62f9011e3" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "5c76c90dea7d659804ad873960906259fbdda3614277ec575d9eec730e747a2e7b9df6716b4c38d3451e319eeecee74d1f4918266fc9239de87080f1ad437b47c6904ed2d5514161ad25e3e237655e00e53fe18d452576580e89b2f1f0f6aa7e40a337fd8c48d690fe013a67264a80e9b5dfd009a9152d559aa02a68f401a09b" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "5c76c90dea7d659804ad873960906259fbdda3614277ec575d9eec730e747a2e7b9df6716b4c38d3451e319eeecee74d1f4918266fc9239de87080f1ad437b47c6904ed2d5514161ad25e3e237655e00e53fe18d452576580e89b2f1f0f6aa7e40a337fd8c48d690fe013a67264a80e9b5dfd009a9152d559aa02a68f401a09b" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "c3471259512d1f03ce44c1ddac186e9a56c1434a6ac567c6" );
            pt_len = unhexify( src_str, "dd5b98b3b3cf03fb92be579068a885afd984630692eb5f155fa6b49f2b1690b803d34b90e8de3cc39c2e61650ffffb51e7ef36d35ad17dc4d91f336363b0734996b162b509c9954cab3dd959bde7e437e9100d84c44104c61e29dbe12492a0272ce6eea2906d390de7808d337e8c650b3301af04a9ed52ab9ea208f3c7439d6c" );
            iv_len = unhexify( iv_str, "50164c63d466148ab371376d5c2b6b72" );
            add_len = unhexify( add_str, "11d1f523888bea1fbc680d34bc9b66957d651efa59e788db3d3f6f50e72184b9d14e9ff9bc05fb687520cf423d681812e007025eedf0e78e7e8191e6b62404e8eb400cf837d762a31aa248553367263d6de091fcf7abedc3e69fc118b7efb0594c89b96c387b7c28ed9a7b75db60b6b5133949b891ff81eca5790a265f12a58c" );
            unhexify( tag_str, "6c5f38232e8a43871ab72a3419ad" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "50438ee712720abf2089331e4c058b30c30c3d17834c507c0010ac3f974a256d01b14a45e9ce5193c5cede41330cf31e1a07a1f5e3ceca515cc971bfda0fbe0b823450efc30563e8ed941b0350f146ec75cd31a2c7e1e469c2dd860c0fd5b286219018d4fbacda164a40d2980aa3a27aa95f8b8e2cd8e2f5f20d79a22c3ff028" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "50438ee712720abf2089331e4c058b30c30c3d17834c507c0010ac3f974a256d01b14a45e9ce5193c5cede41330cf31e1a07a1f5e3ceca515cc971bfda0fbe0b823450efc30563e8ed941b0350f146ec75cd31a2c7e1e469c2dd860c0fd5b286219018d4fbacda164a40d2980aa3a27aa95f8b8e2cd8e2f5f20d79a22c3ff028" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "ec326a1e0fe6a99421398df4fc7d8fea67b67e5f5fcd50ad" );
            pt_len = unhexify( src_str, "6d5016c434a0f4b4a5d9e0b6b8e2d848a94f132f055d2d847e54601a4c9cfc5966a654d696f8a3529a48a90b491ea0d31c08eae8ef364f71f8ec7ae7f7e39bb9c331137b2578362ff165628099944ba8deb0d99ac660d5ed2215b9a7626ff1fa6173cd8dd676c988d16c9cf750a0d793f584c3c8f5fd5d167bc278f4d77a629c" );
            iv_len = unhexify( iv_str, "c94aa4baa840a044dbd5942787a0c951" );
            add_len = unhexify( add_str, "f8401c578f20d9c250ea86eb945184e007a0190462c7abddf238ce1ceddcc230756aa222386d8ba66ebbba13de008ced140896ac55bc47c231cc81370ca9feadc225e017d59890e6291cc4cca27db3078c0cd6cbb51afb62210226a76837c5454728cb5ce3afe7352e7fe75421f94986e6b7b26321bbca15c75ac7c13dc15f50" );
            unhexify( tag_str, "3269922affb9d767f5abe041cc8e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "a7ef81652f604e88a72416924c53979dc73cadd3575eda1c" );
            pt_len = unhexify( src_str, "9ecd19a8eba9fba843486e1bbfb8d9053c5e04b24e30174d4aa89d8307439d653f8630edddafd51719c744bcb4bce3e444847567bd2cdde2995870d0634cc0ba2bde4b6bc2bc583062fb83874a1c25b50aeb945bd109a151772c077438c4d1caaeb5b0c56390ac23c6d117f3a00fd616306fc2ffc4c1e76f934b30fbbc52eec2" );
            iv_len = unhexify( iv_str, "0cc9ae54c9a85f3e9325c5f3658ab3b2" );
            add_len = unhexify( add_str, "d0195b744351aa25a57a99df9573dfa3cebe9850139149b64f7e4af37756a430dda8af98e4ed480e913aa82821c01c1f75b187e105a8f39621757d522c083a8d81d7d8bfe6cf15c439d0692b6affd655a11bcd2457046fae996a1075c66029867b88cd23c503ae04037dd41f27bafd5000d1f516002f9fcc0f2500e8c1b27de0" );
            unhexify( tag_str, "22c2efeddfd5d9cb528861c4eb" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "605271a41e263c92dc14fe9df5203e79d58cc2d1289dc361" );
            pt_len = unhexify( src_str, "2bda3448a283ecba31e0299c0a9e44628cb2b41fa7b1a41107e107cabc381083bdbe048f2804568fdd5fe016f4d607f694042a459ba03a2deda4cccc8cbe4612d8ed0d4575e48bc9f59843369dbe2af6d048e65ff4250e1eef61d7b1b378fe2f3305b133ddc7e37d95ca6de89a971730fc80da943a767ff137707a8d8a24329c" );
            iv_len = unhexify( iv_str, "7f128092a777fc503adc7f6b85eb2006" );
            add_len = unhexify( add_str, "aef9f984fb645e08d5f0aa07a31c114d2f8e9eca047e4a8d5471378cfc2ced1159dc093d174788e58447a854be58942ed9a3fd45f3f4a1af7351e087369a267797c525f134e79709097e733b9003b9be0c569fc70ee3462b815b6410e19954ce2efac121300c06fd9e00542a9c6a5a682fe1010c145acbbb8b82333bdb5ddfd9" );
            unhexify( tag_str, "673afea592b2ce16bd058469f1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "fa076f36cb678e2275561e9553ebdf397360e5a5e44791c4" );
            pt_len = unhexify( src_str, "513305e86c0cb046c5d3720b25a406392766bd1fb7de2758de370ff2e68281e211922890c61f3659460f22c45a57895b424441262a3ba0606df4e2701f38281fd3436a4d0e0f8efecd231808a9ea063dfb725015a91f27cadfe7909a0ee109eac391ac807afed1767ae0515b9c1b51ae9a48b38fe7fec7fe0ddee562c945e5ae" );
            iv_len = unhexify( iv_str, "1ecd53d94fe287047ff184e8b9b71a26" );
            add_len = unhexify( add_str, "5ff25f7bac5f76f533f9edffdfd2b2991d7fc4cd5a0452a1031da6094cd498297fb2a05ae8db71cb3451e4ac33a01172619035a9621d2d54f812ef5343e14b9dedc93838e4cf30e223d215b4d2476ea961a17ac7295069f25b2a12d6e2efe76d91f45632c6d4e61ff19a95d5ae36af960d95050ce98b5791df0b7e322411c884" );
            unhexify( tag_str, "079e8db9c3e6eddb0335b1cf64" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "ce9dafa0e7e53a8766fc0bc38fba807d04e14e5ed61bc234" );
            pt_len = unhexify( src_str, "b585b8bf634757dac015f2f69f2ae674372a664f2115ad2d03bd3e0c335306b02d0947d3cda5991f5c0c25f12ead2c3cc2d65d575fd67091c70bc93ddb4b1e21f7b0fc6e6ae652dea93a6564ff13489f927942e64dd94bf8f821c7ffdef16df58bd8306a957821ac256da6f19c9d96e48eee87f88acb83bae05d693b70b9337b" );
            iv_len = unhexify( iv_str, "fd0751af49814ee98b2b0cdf730adaa6" );
            add_len = unhexify( add_str, "1cba488a0fc8a012f9a336cc7b01cbcc504178eeb08237dbedbc6c7ac68fdf3a6742751a207e43d43068abf6ef4e12a5e3c17e5a2f9398fc04ced67377cbb858fd6020fad675a880adb249e4aba94b96efa515d1cdf5c0c3071a27a3245968867ea94b2bfc2028a67be34c84c3f475944497aa8ca1ab009f8e4b11c8308c1996" );
            unhexify( tag_str, "e5dc92f4ad4000e9b62fb637" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "95f4324b0656bef19eca5570548fc6a7a9923f4e2a7e42066891bc132fd73bc1c9089755d996756de0072824e69c43f2db8ba2bf6f90d3c4eafc0721ceaccce1af896f9fb15fb19c4746979b6d945f593fad61d550f81d12b5945ed728c02931d7f8d917285c22a3af748d75a6bf163fddd84b941d8564c1a63192c816ad6d6d" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "95f4324b0656bef19eca5570548fc6a7a9923f4e2a7e42066891bc132fd73bc1c9089755d996756de0072824e69c43f2db8ba2bf6f90d3c4eafc0721ceaccce1af896f9fb15fb19c4746979b6d945f593fad61d550f81d12b5945ed728c02931d7f8d917285c22a3af748d75a6bf163fddd84b941d8564c1a63192c816ad6d6d" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "8a328554fed68dc4838fbc89fd162c99ec105b36651abbc9" );
            pt_len = unhexify( src_str, "75986f56972c045c850ed68aeb229f203b228fdfc36cad6b16d9bd12037c48700d20d8062a983ffeca76b8d36a67ef51bc8853706e83a34e4e23ff4f4a4eb943f19dbe85e454043d7906be6587a85079f9ccd27962d2905117d2dbeaf725d6ffe87bef52b2138da153ef29b18065b3342b3f9d07837d57b8bc5f2597de06c54f" );
            iv_len = unhexify( iv_str, "e4f7c69a1d026eeebfc45e77bd7b3538" );
            add_len = unhexify( add_str, "e349dcedb0bfcc771c820f0d510b80cef32ae3326484e25aa183015941e7844bc46f617d5e61fd64fa71759e90fcb72ae220bcd507f0fb389b689dd3fa29b3b937eded85f26ada9e0f3f5109f82fef47c7eba7313049750ad17969e7550c0d4093ed18ee27843d082bcee8bf3fc7833d569b7723998595a5a1d871089fd238da" );
            unhexify( tag_str, "8e8320912fff628f47e92430" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "a1ed65cfc7e1aeccd0531bce1dc749c7aa84451ec0f29856f12f22c4105888c7d62e2e2fc8ad7a62748610b16e57490f061ad063c88800037d7244ee59e109d445205280473390336d7b6089f3a78218447b1b2398c4d0b3aac8b57a35891ad60dc1b69ad75e2e86248ceac7bb4cf3caade4a896e5ee8c76893ef990f6f65266" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "a1ed65cfc7e1aeccd0531bce1dc749c7aa84451ec0f29856f12f22c4105888c7d62e2e2fc8ad7a62748610b16e57490f061ad063c88800037d7244ee59e109d445205280473390336d7b6089f3a78218447b1b2398c4d0b3aac8b57a35891ad60dc1b69ad75e2e86248ceac7bb4cf3caade4a896e5ee8c76893ef990f6f65266" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "6e7f6feb4022312de5c804ed1d7a37580d74499107f8cc8b" );
            pt_len = unhexify( src_str, "4f5bbdf575ab8f778549f749f2265e17dc7225713e73ee6d7be163ff7071557dcc2240b0705c079008605f81396414ac64f06b1b637876e04c3fca8d0fa576cef4dd3dc553fd6808eaf120f837f9bb1d9dbbd5cf67ed497167fc7db89d3a84151b81aeab0e921057f121583df5ed7f976b206ece17a913f23485385f64c462a8" );
            iv_len = unhexify( iv_str, "6ce13485ffbc80567b02dd542344d7ef" );
            add_len = unhexify( add_str, "c6804a2bd8c34de14fe485c8b7caa2564adaf9fcbb754bd2cc1d88ba9183f13d110c762a3c5d2afc0fbc80aedcb91e45efe43d9320075420ee85ab22505f20e77fa4624b0387346c1bd944e9cd54055b5135c7fc92e85390ecf45a7091136b47e3d68d9076594cfad36c36047538e652178c375a2fe59a246a79784577860189" );
            unhexify( tag_str, "974bd0c4a8cac1563a0e0ce0" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "46d6e982feff0e7d04a84384c56739b69626dde500e4b7fb" );
            pt_len = unhexify( src_str, "a5160fb2d397b55a7eba02df33a042404188f02f4492d46f4edc03fc67723d64f5f7fed3a60728438703c60454a30f473ac918ffc8f98be5c5e9779ee984415e415ce3c71f9acc3f808d215be58535d3144cebe7982b9b527edbe41446161094d6fc74dec2e0a1c644bbc2cf5779a22bd4117a7edb11d13e35e95feeb418d3f0" );
            iv_len = unhexify( iv_str, "71a6d1e022a6bdff6460c674fb0cf048" );
            add_len = unhexify( add_str, "67a8455c7d3fbfdba3c5ec5f40e0be935fbb9417e805771832ffad06ba38a61b8377997af1f586dc0fa1e3da0b39facd520db1f0ec2bdf1904a3a897f0b507c901fab30a85de51effa9f7d4703ceeb2ca72abe0bd146ba0bd3ffdee11628310db7d65ea1343b018084ea2414995f86fefb45ba91a9dc2236d92078b4305671b5" );
            unhexify( tag_str, "84f1efd34ff84e83" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "991dcaa2e8fdad2b4e6e462a3c06c96067ef5e9fb133496a" );
            pt_len = unhexify( src_str, "9cd0c27f0c2011c1ab947400d28516c7f46d22a409a18fd35c1babf693b8030dfd7822d9ba03bb8fd56a00f9c7149c056640dde690889d2f23978eeeb28ccc26e2fc251220a3682c963f5580c654c1a6736cccb1b8ed104ec7390021d244bd9f92abde89e39a4b83eff8211c8a6259bd6ac2af1da7dfb8cf1355238056c60381" );
            iv_len = unhexify( iv_str, "978913d2c822ba7cc758041d5ee46759" );
            add_len = unhexify( add_str, "5a94dc81af011a8af263318b60215b9752292b194b89f6fc013b0fe8e29133de631d981862f2c131ee34905bd93caffc3b8f91aeb0264b27a509e5c6a41ae781209f8c5895d0d35b3c5e1ae34a1a92a2b979e0e62132051394940ea4d9bfffb8d89ba1e8331b15bdf05c41db83a57745a4a651a757cc8648acdcf850a2f25367" );
            unhexify( tag_str, "15d456da7645abf2" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "f29cff00781f5916930f125489c87d21f6593324d1506f65" );
            pt_len = unhexify( src_str, "a3e8595747b7147d471ac4fe38014bf4a409931e3f419ff88ae249ba7a7f51bd0ede371bf153bab4b28020b7a82a8ca30b75f1e3bcfee3c13db813cbc85138ef05874dedb14a6e5b6d06d7589a83bd5e052dc64433a8e24c1188b9470ddb2536d13b4b7bff0c5afcfaa9aa0157c3aae3b1774df2df14f965d6dee4332edba67e" );
            iv_len = unhexify( iv_str, "50db7ee25a9f815c784236f908bfd7f2" );
            add_len = unhexify( add_str, "ec1482e18692bcd6894a364c4a6abb9c3b9818bb17e5e1fc9ec0b41702c423f3a60907e94c888fad8e78f51e1f724b39969ba7b11d31b503504b304d5c4b4cbd42634f4ec5080a9fe51c82e121ae191270dd2c307af84c82d892d982413a50ccce33698054f761a3fa93da9a1fca321296b378a50d458ba78e57a70da4676150" );
            unhexify( tag_str, "a1e19ef2f0d4b9f1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "eea18261a4de31d8619e77005ebbb3998c5dcfac2bc120ae465e29d6b4c46de7e6c044c8b148ffe4eda7629c243df8af4e7ceb512d5751a3ee58defb0690b6f26b51086dedfde38748f6f0bbe6b495f4304373188e5d2dc93461bd51bf720149a7d3aa543623b122b9af0123b2cdc9020136b041a49498ec4aa696c2d3c46d06" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "eea18261a4de31d8619e77005ebbb3998c5dcfac2bc120ae465e29d6b4c46de7e6c044c8b148ffe4eda7629c243df8af4e7ceb512d5751a3ee58defb0690b6f26b51086dedfde38748f6f0bbe6b495f4304373188e5d2dc93461bd51bf720149a7d3aa543623b122b9af0123b2cdc9020136b041a49498ec4aa696c2d3c46d06" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "2087e14092dad6df8996715cb1cfca90094f030328080ffd" );
            pt_len = unhexify( src_str, "6d039513061980fb195bdf2f7c7079ca4b7e0fdd50d948cbfab5ba10b99e3aea27f08abd000c428851de82cacb0d64c146cd9567e9d55b89819876d6a635bd68bcaf47ffa41e02d9ee97f5a2363bfe6131ae7a21ea5130ae953a64d57d6cbfd45260c5f1946388d445ce97d23ab7ba31a5069a4896bc940a71de32bde02bc18d" );
            iv_len = unhexify( iv_str, "d30504afb6f8b6ac444b4a76115d79d1" );
            add_len = unhexify( add_str, "d95845d268c8d8f9135d310c39e30f55f83ef7ffee69e6ba1f80d08e92ed473b5ac12cc8f7a872bfc8b325e6b8e374609c90beaf52d975f71caeef5ee4c13de08dce80d358ee1cd091faea209a24e3392adcfe01aeb2b2e1738bc75d4a9b7cd31df7f878141cf278d150f6faa83fb3a2fd1225542a39c900606c602f15c06a4f" );
            unhexify( tag_str, "5412f25c" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "1e81a4c10a3440d0002ddc1bfa42ebb08e504fcc8f0497915c51b6f5f75fee3f0cd3e9c5a81ff6528e0fecd68a36192114f17fa1a4cfe21918dac46e3ba1383c2678c7a6889a980024ee2a21bcf737f7723b5735e1ebe78996f7c7eace2802ebb8284216867d73b53a370a57d5b587d070a96db34b5b4f5afe7f39830498c112" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "1e81a4c10a3440d0002ddc1bfa42ebb08e504fcc8f0497915c51b6f5f75fee3f0cd3e9c5a81ff6528e0fecd68a36192114f17fa1a4cfe21918dac46e3ba1383c2678c7a6889a980024ee2a21bcf737f7723b5735e1ebe78996f7c7eace2802ebb8284216867d73b53a370a57d5b587d070a96db34b5b4f5afe7f39830498c112" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "3fc76d627c775de2f789279dc7b67979a9f1cc23c8dcabc9" );
            pt_len = unhexify( src_str, "92a60d38fc687b92d44635aafee416a142d11a025680e5aa42e9ba5aa010462991ad3dd7328ca4a693673410f9bba37f05a551b949ab0d43fc61ef3b8996dd3fc1b325e66eec6cc61ea667500f82a83e699756a139d14be6ca9747ed38cd9b1d9da032ece311331bdcd698666ddc970b8be2b746ec55fe60e65d7ae47c6f853c" );
            iv_len = unhexify( iv_str, "8f6fd53eb97e12dcd4d40f2843e25365" );
            add_len = unhexify( add_str, "e56995df73e52606a11de9df6c7bfb0ef93b86bf6766e319aea59372060294b0e1b13c6288c2310a4bef725a2dddb174f3e1228649861757903c4497a0eec9c141454fc75f101439a2150e368857c4f0f6e5161c42c77f632bf1c229a52595cbf16e9018de9a8f6a1e6b8b18bd244f93f001eb2eb315405d223c0d27ece9d4d9" );
            unhexify( tag_str, "613ba486" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
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
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "b10979797fb8f418a126120d45106e1779b4538751a19bf6" );
            pt_len = unhexify( src_str, "e3dc64e3c02731fe6e6ec0e899183018da347bf8bd476aa7746d7a7729d83a95f64bb732ba987468d0cede154e28169f7bafa36559200795037ee38279e0e4ca40f9cfa85aa0c8035df9649345c8fdffd1c31528b485dfe443c1923180cc8fae5196d16f822be4ad07e3f1234e1d218e7c8fb37a0e4480dc6717c9c09ff5c45f" );
            iv_len = unhexify( iv_str, "ca362e615024a1fe11286668646cc1de" );
            add_len = unhexify( add_str, "237d95d86a5ad46035870f576a1757eded636c7234d5ed0f8039f6f59f1333cc31cb893170d1baa98bd4e79576de920120ead0fdecfb343edbc2fcc556540a91607388a05d43bdb8b55f1327552feed3b620614dfcccb2b342083896cbc81dc9670b761add998913ca813163708a45974e6d7b56dfd0511a72eb879f239d6a6d" );
            unhexify( tag_str, "28d730ea" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "dafde27aa8b3076bfa16ab1d89207d339c4997f8a756cc3eb62c0b023976de808ab640ba4467f2b2ea83d238861229c73387594cd43770386512ea595a70888b4c38863472279e06b923e7cf32438199b3e054ac4bc21baa8df39ddaa207ebb17fa4cad6e83ea58c3a92ec74e6e01b0a8979af145dd31d5df29750bb91b42d45" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "dafde27aa8b3076bfa16ab1d89207d339c4997f8a756cc3eb62c0b023976de808ab640ba4467f2b2ea83d238861229c73387594cd43770386512ea595a70888b4c38863472279e06b923e7cf32438199b3e054ac4bc21baa8df39ddaa207ebb17fa4cad6e83ea58c3a92ec74e6e01b0a8979af145dd31d5df29750bb91b42d45" ) == 0 );
                }
            }
        }
        FCT_TEST_END();

    }
    FCT_SUITE_END();

#endif /* POLARSSL_GCM_C */

}
FCT_END();

