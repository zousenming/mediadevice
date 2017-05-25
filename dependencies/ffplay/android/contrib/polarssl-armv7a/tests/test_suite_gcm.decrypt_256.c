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

        FCT_TEST_BGN(gcm_nist_validation_aes_25612800128_0)
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
        
            key_len = unhexify( key_str, "2c186654406b2b92c9639a7189d4ab5ab0b9bb87c43005027f3fa832fd3507b1" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "3a0324d63a70400490c92e7604a3ba97" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "4c61cd2e28a13d78a4e87ea7374dd01a" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800128_1)
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
        
            key_len = unhexify( key_str, "747d01d82d7382b4263e7cbf25bd198a8a92faabf8d7367584c7e2fa506e9c5f" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "7156358b203a44ef173706fdc81900f8" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "9687fb231c4742a74d6bf78c62b8ac53" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800128_2)
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
        
            key_len = unhexify( key_str, "1cbe30216136b7eaf223e6a7b46c06625176d9a08182fa806a63d8b143aa768b" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "4fe6ace582c4e26ce71ee7f756fb7a88" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "d5bdf8ec2896acafb7022708d74646c7" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800120_0)
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
        
            key_len = unhexify( key_str, "f31194c83bb8da979a1eabb3337ceb3d38a663790da74380d8f94142ab8b8797" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "404efd26b665c97ea75437892cf676b6" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "e491075851eec28c723159cc1b2c76" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800120_1)
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
        
            key_len = unhexify( key_str, "daeed52ae4bf5cbe1ad58ae4ccb3da81fb9c0b6f7619ca21979313ad9d3e83c1" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "4037eadb11249884b6b38b5525ba2df4" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "360c6ef41cbd9cd4a4e649712d2930" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800120_2)
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
        
            key_len = unhexify( key_str, "3ad81c34389406a965c60edb3214663ac4a6bd5cfd154ae8d9dc86dae93def64" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "cebbce06a88852d3bb2978dbe2b5995a" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "bd7ca9f6bd1099cde87c0f0d7cc887" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800112_0)
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
        
            key_len = unhexify( key_str, "4c152ba30aefa5b2a08b0b4d9bf3f16fc208bb0bc4c4eca9411dc262d9276bad" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "008d040fbd7342464209f330cf56722c" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "c87107585751e666bedae2b1b7e8" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800112_1)
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
        
            key_len = unhexify( key_str, "9aed4ae6b1d857fdcbe5aec6db38440613dcc49f24aa31fba1f300b2585723f1" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "947c5f0432723f2d7b560eca90842df1" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "7d331fedcea0fd1e9e6a84385467" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800112_2)
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
        
            key_len = unhexify( key_str, "cc80bc031676eff5f34dd076388a5130e985f9e06df4b4bf8490ff9ff20aae73" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "51f639467083377795111d44f7d16592" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "02d31f29e15f60ae3bee1ad7ea65" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800104_0)
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
        
            key_len = unhexify( key_str, "db7a40213b5b4b07e9900dc28f599403b0579cbce13fcd44dff090062f952686" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "aea6f8690f865bca9f77a5ff843d2365" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "7f2280776d6cd6802b3c85083c" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800104_1)
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
        
            key_len = unhexify( key_str, "299b874eaa8b7baf769f81f4988a41e2708ae928e69a5ba7b893e8e6b2db5c3b" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "2aa04d85d2c0dc6f5294cb71c0d89ac1" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "ea01723a22838ed65ceb80b1cf" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800104_2)
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
        
            key_len = unhexify( key_str, "a6c7b4c8175db4cf23d0593ed8ea949043880fc02e2725f0ab90ae638f9dcfce" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "ae07f8c7ac82c4f4c086e04a20db12bc" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "1132e4fff06db51ff135ed9ced" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280096_0)
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
        
            key_len = unhexify( key_str, "b98e1bf76828b65a81005449971fdc8b11be546d31de6616cd73c5813050c326" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "929b006eb30d69b49a7f52392d7d3f11" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "33940d330f7c019a57b74f2d" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280096_1)
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
        
            key_len = unhexify( key_str, "09ccef64ae761a70fe16772cba462b058a69477c91595de26a5f1bd637c3816f" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "e34b19381f05693f7606ce043626664d" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "2adc2c45947bfa7faa5c464a" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280096_2)
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
        
            key_len = unhexify( key_str, "654cf46598e5ad3e243472a459bcd80f1e026a65429352dbd56e73fcc5895d1c" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "a56f27709e670b85e5917d5c1d5b0cc2" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "177b9a5e6d9731419dd33c5c" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280064_0)
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
        
            key_len = unhexify( key_str, "84bca1b2768b9202bf194f2d5e5a0a5f51fd8bb725f2bab8a3fccbdb64a4ea70" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c45b2708c5bdf65ec6cc66b6dfb3623b" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "fe82300adffd8c17" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280064_1)
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
        
            key_len = unhexify( key_str, "c8ae011795c9a60ad7660a31fe354fa6f7e9c2724d7a126436291680cd95c007" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "1bd9ea6186450f9cd253ccfed2812b1c" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "35214bbc510430e3" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280064_2)
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
        
            key_len = unhexify( key_str, "df2f0a8a3849f497d12bda44e12ce30a6957f3febcd5ec9bc134171326ca66d3" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "728cb9608b67a489a382aa677b1f4f5b" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "e2ef5d9cc5791c01" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280032_0)
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
        
            key_len = unhexify( key_str, "78e8a8ad1ecd17446cf9cd9c56facfd4e10faf5762da0fd0da177f6a9b9c3a71" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "f169ce6f3ccc58f6434ae2b8ad1a63a1" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "0fe57572" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280032_1)
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
        
            key_len = unhexify( key_str, "02ca6d8a862e25db9d68e4404abc107e700135df4157cfb135ce98eaa33151c9" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "7b722fdd43cff20832812f9baf2d6791" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "72dea6cc" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280032_2)
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
        
            key_len = unhexify( key_str, "9a2b709dbcc3a4fb15b3ad541fb008c381b7e985b57df52f07ca7cd26ab1ecc4" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "729baa4c0ef75ed8aae746376b39fe3c" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "2a0d607c" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024128_0)
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
        
            key_len = unhexify( key_str, "449d39f863e4909984b37f2e5c09ea4d4b3e9fac67bd57c299e4e1d1f084aaa3" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "d8e9118f331bb5a359f0aa8882861b72" );
            add_len = unhexify( add_str, "4ddcae0bc24d622e12bdeaac73e8d1ab7957af051d27dfaafce53aeed4cdd3f989ea25989a2f41cfb3c38dbd841c5560b0b5ab1861b1fbcd236865d13da55b50219462e021f8a21848a64a85326031fcec8fe47a6ef4a435dd2b2fff637644ffcf3914ef2dfa5dd556421bfd297be150b31db039f0f2cc422b282e659e70cceb" );
            unhexify( tag_str, "c595b9d99414891228c9fa5edb5fcce3" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024128_1)
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
        
            key_len = unhexify( key_str, "3e70e66813fc48f984dcda4d1c9c24f1d5d1b71ecfc8bb9581782e7cca5a5cc6" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "d804f1051e72c9b7117002b862eb45ff" );
            add_len = unhexify( add_str, "0b1ab2b7a87cebac668c7a532fa8fa56a22cabf0c41fc1e6744ffe07c857c6865d623f508351f98f3f0c577d1eb94300a30a445472218c8ac626b0bee7d4c122d33f8130436a89add341e8ef7e00694afb4ad80d314d87ad3f921c7105eed05431b8151df7cff2c8e3790efd4acd3f60332dc7f34fdd90beef70f9093361d65b" );
            unhexify( tag_str, "c09c2e3fdfefa222f7345ae4efb978fc" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024128_2)
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
        
            key_len = unhexify( key_str, "8e534041090b45b80f287dc5fa20ebda017ad81b0530e680f62c6280fd8881af" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "ead675b019ef5c6bbf4985f2a382d6c1" );
            add_len = unhexify( add_str, "b1db220052c4bebcef27eed6db0dc91be481179d71160c5a2ddb2fe497a05484840b04cce48980057d770fbbd0d5f3d5c633b55470617ad2cab5767188283310337825c4b0eafe13b5b11293dec230dad43b220885105767938c7ec4600fe063f98aa14bc6afb886fc874c10546749da295f571e696305bd9165486e29f43f52" );
            unhexify( tag_str, "9aa0cdad5686ca515cd58aed94938ef4" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024120_0)
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
        
            key_len = unhexify( key_str, "2de18874470c09db683cf45cd752bdfa8bf33e7967220b1a69f41f2a02da1d80" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "af30eb2d0a0c2a50ea413f3285aa88d4" );
            add_len = unhexify( add_str, "22889b868d8ccc9f488406813caed199b23091ddd796c8632f564e7cf5a39dfb725266a931fec958659b6fc5b6b9343b8217edb0acb010afc9416601155262b57bd398d62f555953f0e15958e19ae004fbc9cb25e0269a9eaa38a4635a27bfa719fb249fa49337796bcf5f416bba87fbf3b19f0d8c11290c25ca50bbdc822f01" );
            unhexify( tag_str, "646bbc9b14681af65b0d1c4c9f1d0d" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024120_1)
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
        
            key_len = unhexify( key_str, "1a1bb9122e762ecd7ff861a1d65e52607d98e7ae5bd1c3a944e443710f3b0599" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "32f99ea4cbf52c2701c2252e5e6c863d" );
            add_len = unhexify( add_str, "91b7a70c3a06c1f7f2ea584acb5dd76177ba07323c94f2e8f7cbe93fc0bb7c389c3c88e16aa53174f0fc373bc778a6ccf91bf61b6e92c2969d3441eb17a0a835d30dcf882472a6d3cb036533b04d79f05ebfaadf221ae1c14af3f02fa41867acfdfa35f81e8a9d11d42b9a63288c759063c0c3040c3e6ee69cf7c75f9c33fea1" );
            unhexify( tag_str, "a8e29e08623a3efdbbe8b111de30a4" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024120_2)
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
        
            key_len = unhexify( key_str, "3bfad1e8f9850577f9ba3f290e9a5e91b494c2d99534220362e171a7543177ac" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "8410886b70c57d7ded8596443bd1b157" );
            add_len = unhexify( add_str, "ca801c83596795515ea931edba00e06e332bf84246b7036e10b317e2d09a51b2981fcb664ee3bf4180bb0b12ed1cda221abc6790b27c26914f5ef9cea9536e2453cd5b247cb054e295c2687b725a97cbc484b8eb86c6ceee03bd07a54a9301a3ac0ddb23aecb825a238252e7575329058b40e75575a7f16439edf5be163ce5f5" );
            unhexify( tag_str, "e3645db0c600dba52044efcecfc331" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024112_0)
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
        
            key_len = unhexify( key_str, "65debdf2f2191a6cd8de8ad4d5d4d0d8f731f67744e2545df6b2a7cba89c1ee0" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "fdab2ee547dd8b6f5a4ea2dd19697b3e" );
            add_len = unhexify( add_str, "d2b0a0438ee0f145aec9a7ca452b788ecb473152b78fb75f6ace721afc7b0ae1942049b790f3a5b6221a8760295659756d35347cc04029be03459f3e23a71209b4e0bbe13a253a888c83db23376d3a6d9a539f7c9fa4a12dc64297e7c93dfa0ab53ef76b6e1d95bf6f3d5e6ee8f08662fc03ec9d40eff0a43f23ac313671bfd9" );
            unhexify( tag_str, "c25fc157c3f2474885e2eea48aea" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024112_1)
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
        
            key_len = unhexify( key_str, "496ae810380460d40cd2fdae8c0739f16b87205cc7f57db0a71a473eb361d570" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "77233de96f5e1744337778212b411bd5" );
            add_len = unhexify( add_str, "85f5b54b4c4af5c808120bd28d98e44e96f4126623e57684957e9fc4fd1a2d0583940b8fc8314a249325476e8d05247831b04709580ae714e8187cd38f9559419e14c9fc4f8c454ec191b8ef2a3610988fe3339d0dc6b72f5978f9eff9d596dfabf27056e3a908c6497267461386e860f6b9d65526294bcb92908b5661b06b5a" );
            unhexify( tag_str, "4ed91af6340e70b0c2b94ab6f82e" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024112_2)
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
        
            key_len = unhexify( key_str, "aca188183b46139cc7cffc82a6aaaeb2fd73cecad14e75c663bd62daf1ec711d" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "7bbf7fb55eb70cce94cc6a2b67de55ba" );
            add_len = unhexify( add_str, "015cfba90f069545fed60f31992ff3d3c3592eb91e7a53df5978ded64291954cb99a57de82d5398ce782b68d14ac04a8b425395bd076ead59eb445721bdb2f45e19fa089117800cbbac7b8313fb165ccb1122acb654e1242dc7fe6885ea1cbb7281b1270cfa1549cdfe9b47caf47b4ac3807e562e48c066566f5e606b5023b47" );
            unhexify( tag_str, "3bcb5c2a4261d75bfa106fb25ee1" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024104_0)
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
        
            key_len = unhexify( key_str, "8cd6815f6ec15f03b7a53f159e877a5981e0ab7f6e6c261ddde4b47cbb2f2366" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c431c07d9adf5f61204a017259cddd75" );
            add_len = unhexify( add_str, "4e1a835402bde4f5227e64b46a1f8d0f23a9434e189377fcdf1b9621ba1987eb86a7f3b97ed0babfd674e74c5604a03dd016d71000a72bbbd00a7f7fe56ad0fcb36a3e24dd0fdb63bd66d4db415f35012416ed599796ca3f678df7eb5a1b17f75abb348ddd3b366369a7b362c9488aedab836b61f9a158f0b129c8ca0a53a81e" );
            unhexify( tag_str, "0e463806ff34e206f703dd96b3" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024104_1)
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
        
            key_len = unhexify( key_str, "8f0a72abcda104aa7fae501f9a3b686d00d3f6fe984731db8a2865bfec587073" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "ab8acd063775d1b1314f14e90fddd1be" );
            add_len = unhexify( add_str, "02c6d426e7f20b725d8cde0a6382e49b029b52126889013ef45251f27b2fadb95ca4a9a3b16ad06999eeca4a473e813045db4942e9b9ff2e5a5e429d9bac298372344d1b781d5facabf6d779643f31ada6124eb50aad599044b54279ec9b25714ac8a3b9ad2487cec7f4b1ee245d7be3d496d6af1d4cbee1c8201312541f3064" );
            unhexify( tag_str, "3f0ccc134091e0c0425887b1b9" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024104_2)
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
        
            key_len = unhexify( key_str, "417135cad74280e6f8597dc791431c95cb8fa63bbf7197e3ab37c4b1d6d9438a" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "0fe22d9ba1d0e32656e3a9f07a517a27" );
            add_len = unhexify( add_str, "a0b2712e81d329d5b076a4be2ad6823cee6dbd17d9a592d065bdebb92b1ff37a56bf2f5e5341f39c574246ccda19e5f35fede49c9ba958f3920cc5440fb404fab7846884ca0c2a3af5b51f4fe97a1395571319cc5b40f8aac986d77de280db82343983982638326ef003e0c013af19c34672975dc99ccc0853a1acf7c617d965" );
            unhexify( tag_str, "888b836c9111073924a9b43069" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102496_0)
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
        
            key_len = unhexify( key_str, "304824914e32ea0efd61be6972586093349bd2cc2cf0cff44be943682b2dbff5" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "b6d927a71929029f6766be42746f7cb1" );
            add_len = unhexify( add_str, "7281c81c7514f4b17cb125c4649006ef8959a400a1e4d609d277e363e433725fa32346a10bcbd826b6afc8222158920d0a2db1e6fc915e81231c34c3941ecf3c6f94ffe2136190cae3dc39a4277acbc247f36291b5614a8433b1a0780434a6c50521b72ec25145bbd3b192647155d5dd9df9e66762d39592602ea99bf9bfff49" );
            unhexify( tag_str, "b6044c4d7f59491f68b2c61e" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102496_1)
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
        
            key_len = unhexify( key_str, "8a10e9abe9389738e12a4bb6f553ae81e8bd320e0dfbc05fbae2128c1fde7a23" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "6da44354e198e3beb54792718becbcc1" );
            add_len = unhexify( add_str, "199d754630135b669bf2ec581d3027a569412ab39a78dd9d482e87b778ec65c6473656260c27827e00e566f1e3728fd7bc1853a39d00e43752c6f62c6f9b542a302eea4fd314473674f6926a878ec1e4b475d889126ce6317115aea7660b86ab7f7595695787f6954903f72361c917523615a86d6ce724bd4a20c9257984c0c6" );
            unhexify( tag_str, "5c5683e587baf2bd32de3df5" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102496_2)
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
        
            key_len = unhexify( key_str, "d164ffde5dd684becaf73e9667e3e6acb316682c41aea247899e104a54dd7a7f" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "1d388e19e9d7a9750e2fc1187d4b075a" );
            add_len = unhexify( add_str, "f166a5b6f91261cda56f1a537f42ffb8aed10af5e0248f8910034b92dbc58d25953f1497f571d31fbf5ec30d92234b440161703851f0e43530418147ce6270fbcb5db33ab819ba8973051908704b6bea8aaca0718947e6aa82498a6e26a813981783ed9bf9d02eb1ea60927530c4700ff21f00179002b27903dd4103bbc5c645" );
            unhexify( tag_str, "52e10495105799ead991547b" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102464_0)
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
        
            key_len = unhexify( key_str, "2854188c28b15af4b8e528ab25c0950fc1384976f242716c91bddeec06f2fdea" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "075af9c31f5252b8920092cbd999e7a0" );
            add_len = unhexify( add_str, "e9452f71093843a025bb5f655eb6a4e8316ab5946484b11818f22b62f4df75d5891fa3397537093a261dc9a7648b7477ea1f5fc761716e302763364bcab7992595edd0fc1c7f7ac719c879e6616e2007948eb8530065a6cccf73d0fe4a0598819b471b0856e6d90ea0fc0e5d36a30ee925b6b8e5dbf40e77f01efe782c0bb4f7" );
            unhexify( tag_str, "6ff8fd87e5a31eb6" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102464_1)
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
        
            key_len = unhexify( key_str, "2bfc445ac0365ae6c3c3815fd18bbd0c60ea224f6620d9b6ac442a500221f104" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "43c5f3367a9955aaee1a0c4d4a330059" );
            add_len = unhexify( add_str, "db0bae8ce7c66a8ba2fedec22f236212e9a7ad72b371de285c7dc6d2f6c22df0ce4920e0f03f91eb1653c4490050b9f18a2a047115796f0adc41707d1ffcbf148aed5c82013f557e6c28f49434fc4eb20112f43566f212c48cec9894ac40772fcd9b611ee9444df7b73e35b8a38428ccb064c9c50491d2535e0b539f424db83e" );
            unhexify( tag_str, "49aaa806cb2eeadd" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102464_2)
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
        
            key_len = unhexify( key_str, "7b828f99aaf751bf22d993ed682e488595617a607ed74aaacbb6b60457453080" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "d48dac1d8d77e245420feb2598812418" );
            add_len = unhexify( add_str, "f50f785f4e7c848a55a616ecf4b6b1e1ca85e16de7100c7e4273d411bd95c1380ee157ba501ba9616980195f34e39f43e335f33253342feb8ed64443483c721b85241a0320b3cac83104de2db47188c61a373fba592ea16feeefdee1f2bb43927396f58151418672ebb74afff5c029503a0d0be81430e81ed443e08b74c03183" );
            unhexify( tag_str, "a5b71ecf845b25d0" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102432_0)
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
        
            key_len = unhexify( key_str, "7b6da11d69fca3e4c907628d3eb63d95c7e502fc901372fd097e064e70831432" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "6fe2148f250ea178d4c8ca8423ead87d" );
            add_len = unhexify( add_str, "a8097bb74ded776f578eb7588f5ef8915db9bfa7262af700c8e76ee114e07557b6786dd5a60a66b2703e7c9de5d6b42aca92568aec5d1ecc298dbd0edb150b8cc13c9a78698f7674caa94da6cacd1f3ef4ca4238c59830ea725ab3a6284e28966c8c32d9bccfb0cfd6583a5ca309debe86549a6f317d15c5f928cbc7f473310c" );
            unhexify( tag_str, "e9cdbc52" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102432_1)
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
        
            key_len = unhexify( key_str, "c5ae9328be49e761064080fc213e53e373fd86359a09d0355e2d438d9b8e68f1" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "a7e3f8660ff925d5c88c5aceffbd7026" );
            add_len = unhexify( add_str, "2ddddba7a56cc808aec4602f09ae9bd78887827bf0315d8dbe16821606ef9d117746dd138bf1f23565d1ab8f4cee36d53fe3730632c5df9f12109b16edbeae285bb49dfdd155f5dc97b319a85362d53cc86817b7c1c31e5e87c9f37422f133d00dd0776bd92ab05ce6860573cd911645cfe3fbe515e85f744899a447fe443653" );
            unhexify( tag_str, "e35dbac8" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102432_2)
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
        
            key_len = unhexify( key_str, "e4f8ca13ba86c658cc7f42d4f029422209efbd101bc10a1df81a42cfb3a0f79f" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "1a362fa0e4054ba11e4b06d59c8bc9cf" );
            add_len = unhexify( add_str, "e7ad5c75aa13659f8ce4b1650c46382645ec67418199b84ea445b8ceef619ef3fbde59ed3d313c459e36fcf87d26ef2b453409b32f1086934c3072c1ef0aac83762d28b1193b9afff2c083ce4300b768b0ae23ff9d3dcf65bc1693f1350da65180620aab205aceacfc683c8be53a332e2d0337a7518d2a5204f9c8d7325a4799" );
            unhexify( tag_str, "e7a37f15" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240128_0)
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
        
            key_len = unhexify( key_str, "00050a21ca1e72cd0924be31b943c60854be6744577de3dd9d1f4fada4a19ea6" );
            pt_len = unhexify( src_str, "693ffd3d92294857a99c702a0799eeca28ab066dd90917b9ea5ef8f6547f1d90b106cbec8ef2c22af9f8efa6c652f2f97c2baf33af14fe9def230d49524bd65909c3df1490f637f99e788dcc042b40e00bd524c91e2427ef991bf77e7b2f770cda6e90076c5dac4cac7ee3958b53ff8ce846c3a96281f53c2c52f5f3e523536f" );
            iv_len = unhexify( iv_str, "2fc1afc1395d8409919248709f468496" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "e39b6a7fd5ac67a2a1cc24d5eb9d9c74" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "cfcd6b9ff7641829cbadeaa2e56f1f150a099eccf3e378fa4da59794dcc4490aa4f9c5db0ab245bec36a7d4557a572008e42f03bc1baff3c946f23f54a4dc9828f106cf4264e4ab40165839d1085e7795b1ae0950f0ee4a08e46ada501b6b51dee0e518129c9426e5bd44c66674a9f99cfe676f002cfd344c5bbd22d3d91e600" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "cfcd6b9ff7641829cbadeaa2e56f1f150a099eccf3e378fa4da59794dcc4490aa4f9c5db0ab245bec36a7d4557a572008e42f03bc1baff3c946f23f54a4dc9828f106cf4264e4ab40165839d1085e7795b1ae0950f0ee4a08e46ada501b6b51dee0e518129c9426e5bd44c66674a9f99cfe676f002cfd344c5bbd22d3d91e600" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240128_1)
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
        
            key_len = unhexify( key_str, "f10965a66255f0c3515af497ccbb257a09f22ec2d57c5edae322a3e6d2d188ef" );
            pt_len = unhexify( src_str, "91598690edf2de8b27f9bc7461a84e80811cee544f0542923898328cf157590251f0342cb81d359b5dccc5391a12320d1444c26f24178977dd6705c2b365dc1ece0152c42e2f0ee3162cf886ef5529f4f16a77f3bdd2aeccd405b59addf098521d0d38cc25f1991e11be7ecf24caedb48a2a286d2e560a38fa9001c5a228c4d1" );
            iv_len = unhexify( iv_str, "c571ce0e911de5d883dc4a0787483235" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "6d9d3a5dbc8dce385f092fff14bfffda" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "2867996e389e09ec0da94d42e77b1e436b50065b09ca4adf1cd03240444ee699dbb7b3fc081a1869ca607d77d5ff9754fc3c997ff0a4ee17543a2ba77886b88a7128bcc51d3450df58ff3a26671b02c1d213df6adb6f7e853080eb46b504517cbaea162710a9bbc2da8b552eb6b0e0cb98e44fcab0a157312be67974678d143e" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "2867996e389e09ec0da94d42e77b1e436b50065b09ca4adf1cd03240444ee699dbb7b3fc081a1869ca607d77d5ff9754fc3c997ff0a4ee17543a2ba77886b88a7128bcc51d3450df58ff3a26671b02c1d213df6adb6f7e853080eb46b504517cbaea162710a9bbc2da8b552eb6b0e0cb98e44fcab0a157312be67974678d143e" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240128_2)
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
        
            key_len = unhexify( key_str, "4437ee7d16d8c3ca1aa01e20b66749efa901614d4bb4bee786ad5a5f1bfde2e6" );
            pt_len = unhexify( src_str, "ff80727a3485cdbc7fab4ee9fadfdc621c538e2055706629046078f1aa3fb687fc728d3a7ffa52ae457b7b5649613eab7bafa464bb435314c49e5900750f7ad39ca9b75df6b2eaa755439e101f67b7ae4cd80dc4a9dea0027048253f2d0a6014056ca69b8c85605b00cf75fa7634a0ddf464270a8c79ce1a1324c4a4c513b24b" );
            iv_len = unhexify( iv_str, "275393276745bc43bae4af1e5d43a31e" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "a82ff1e87d26e4d6e417b60fb2d3ce23" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "88f994d276ed20be3932d16f551c4b7e2ed80411f2e72ce098fa0b70c22157a59edab30649fec447dd63f0c87dceca7238ef0d9561b58489ba7bd86f2892743099f40af63c432f78ac0ad0b5c2be47b9e3045e7237b096ee400f430af63a6f309de785caf190f3f4aabbe79f727a741590de542bd343df68d13db55a5f8bab41" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "88f994d276ed20be3932d16f551c4b7e2ed80411f2e72ce098fa0b70c22157a59edab30649fec447dd63f0c87dceca7238ef0d9561b58489ba7bd86f2892743099f40af63c432f78ac0ad0b5c2be47b9e3045e7237b096ee400f430af63a6f309de785caf190f3f4aabbe79f727a741590de542bd343df68d13db55a5f8bab41" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240120_0)
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
        
            key_len = unhexify( key_str, "fe4ec037ce563dadee435cfcb2bf090f1f7ccc7d1b5b4fab2f1b738348f8ed2f" );
            pt_len = unhexify( src_str, "64eb8a4bda9804c09b04cfcd89094928c21480908b81ee19d6c29c2a3631b1a5bdc8e7f8ea56f7b8b8e14a5208296026785cac3a6afa54be8af4d5faedcd12b6621bde0f8ec5a2635fe72a89468ca7704c73aa40cd2ba97aef08886b27a694d339b00e7d12a31308672f87c06a7388a1432f869eb4cc1da864140b1b33931925" );
            iv_len = unhexify( iv_str, "47f5264f7a5b65b671892a05fa556f63" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "660462b4088f6628a630f2e4170b21" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "4a310e035361f98b8c54fb4cef70b1a9c910552ece056ca8fdab54c52308ec0ad7fe9dd1dae92badab5010577de522088768fa6466fbccce22e14c51ca7986c4063d0f06bf578dab16a91856713198a7138395c49c78b6314b57ab72fd079028c8dc351952d90b04a7cd2b245df0c0522447cdb7d3329fd9425fe5cb40a8e7c9" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "4a310e035361f98b8c54fb4cef70b1a9c910552ece056ca8fdab54c52308ec0ad7fe9dd1dae92badab5010577de522088768fa6466fbccce22e14c51ca7986c4063d0f06bf578dab16a91856713198a7138395c49c78b6314b57ab72fd079028c8dc351952d90b04a7cd2b245df0c0522447cdb7d3329fd9425fe5cb40a8e7c9" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240120_1)
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
        
            key_len = unhexify( key_str, "e6e1ada628ca76eb9832cc6b5efc5c9d2686bb587366a6de2d734233fa95279e" );
            pt_len = unhexify( src_str, "a0ac738e0fb35246b84a6fbe319f827039515df25d0c0fc6de7c048253ae63d3c561e44a12672ffeae1cb925610b482aa422bbee0e1784fc69baac3a97d69f51e6d2a17957b44b318624ea7ec680a559f4d3f2761d09bee66efb3a312ae6b3ecb673e756b2a0f654671e82500e7ace91f2be2a74bc3bc1ec1a4b6877a53c27c8" );
            iv_len = unhexify( iv_str, "5a100b451e3a63a3e6d4b8a9e59c6bce" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "88df9a1ea54e5bd2ef24da6880b79d" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240120_2)
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
        
            key_len = unhexify( key_str, "cd5c1e90d78213155c51767c52c290b3d657db8414ee0a7604a2ec7b48105667" );
            pt_len = unhexify( src_str, "8e987693da0fb77b6d1282eebd3a03e05d9955ff81929b1a2c721574862a067ddee392c7ece52ca1451f3e6e321d7208882d97b4149af6d78d65c054e1bfcdfa62bd2202de32dea8363f8d7f041891ce281840f3cd906ab46ca748e5b3b11890b4014bf0271c9427c874097782d1c13dbb40e78fc8276fc134f3c29923a43a01" );
            iv_len = unhexify( iv_str, "4e022d8d86efbd347e8cbab7e979771f" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "e7df79af0aef011299c3b882e3a45b" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "3b20473d9b5018d089e7f74d3fef22ec2805948a9e07689831973c704a6d8db4d090af88d696ab8c3aae9740a2bbd7f03e0b18b2b591e59c335c1043a2578a89b1a9f20fd0dd53f12e00e9bfdb27de8caac772bbfc4de9e4a255a5d1b04e59625a87b8279babe613def58d890d5502abf2f709aab625dcc20c58772832c7bbab" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "3b20473d9b5018d089e7f74d3fef22ec2805948a9e07689831973c704a6d8db4d090af88d696ab8c3aae9740a2bbd7f03e0b18b2b591e59c335c1043a2578a89b1a9f20fd0dd53f12e00e9bfdb27de8caac772bbfc4de9e4a255a5d1b04e59625a87b8279babe613def58d890d5502abf2f709aab625dcc20c58772832c7bbab" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240112_0)
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
        
            key_len = unhexify( key_str, "6e3dfc07003bb6a2d82bd5263b2832f47db4e73279266c7a9ea21f4f18eddf83" );
            pt_len = unhexify( src_str, "a960da222af9d4da5797e6957d59b00f6d3893599c70e95c0984b56eb3329b191703c2532f3288b15ebf655b9b5ee4617484e5ac9c39bb06731d03ebe4fef9495d003b0ed694cf540b4dc759d32629e55512680badd81234bd71ffd55fcb5e6a85031c1dc31ee1ed198939582d8336c905717cc87101dcfcf9d833fac815c8ea" );
            iv_len = unhexify( iv_str, "7c0f49fb54f5e68c84e81add009284e6" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "b2ec0f3da02a9eb3132fb4ebe3b8" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "a40b6f70f0572fe0bc70d83368e7c154f7dbd501f52501630a2e523d18e216e07368521f6040d806299397722b99bcf7f85d36b8bed934b49aa1fa76d38783e6a2e392d6d0786d467f7bc894a739ecf94f0fe884a9c391154f8326bf31ea5242a18aa263d04da4b63b11de23b42d3e10a2d5460cb32700cdf50a0d89165ba22a" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "a40b6f70f0572fe0bc70d83368e7c154f7dbd501f52501630a2e523d18e216e07368521f6040d806299397722b99bcf7f85d36b8bed934b49aa1fa76d38783e6a2e392d6d0786d467f7bc894a739ecf94f0fe884a9c391154f8326bf31ea5242a18aa263d04da4b63b11de23b42d3e10a2d5460cb32700cdf50a0d89165ba22a" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240112_1)
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
        
            key_len = unhexify( key_str, "4103b1ddff87a508a219c808a04ad4750668688f4c2ee75b92d28d70b98a2c94" );
            pt_len = unhexify( src_str, "a00a196193ff07006b7df524824bd0971d63f447a3a7bb1b75c1e2d11789482c115cff677b54948d36dc4de34200bce97be0101d88cee39b177857dd5da3cb0d2f9d6e1150f72a3bd655e0bace1d25a657ba9a7f8dff082b4460432075afb20173da22b49beeb6a030d72ba07869ff4389fc1c28d87018d7c1a9829c21932197" );
            iv_len = unhexify( iv_str, "5cea906737518c2cb901016e30206276" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "3a3a771dd5f31c977e154ef5c73a" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240112_2)
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
        
            key_len = unhexify( key_str, "cd8c2f0c330d5db316dae7a16b57d681ca058864f7bd60f3d0de174442283f77" );
            pt_len = unhexify( src_str, "e2a5ad295d35031535bf13c2993bd0b292e8a9465b9dab738e59ba03670248a1ecc92b38a55bae34729162271cc1572c35fcccb27417b48dfcbff852a7a8845cc829a4461061b558ac8b5930a5c6491ffba04a9d0dff220b3cd5e4fc2e0f3db3b2ddd90328f2cad819573a7856299620b02f5ee0267f3b56981afbf1b7d9e3e1" );
            iv_len = unhexify( iv_str, "387ee8c1e7f047e94d06d0322eec02fc" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "62356850d12b54e39872357cfa03" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "17b7f6bdfc1993c56dd9bd674cc276a55a46fdd9fd5fe435b9e4b7ebc7052a9dc76a99e4e43aba7d486603189c90d10a21ad3722c86bf5bc856a0f930ff5bca65be708b76bb8a29105da67f31eebcec81f28aaf526d2f8f0feac393a24959dcd612e2b93b4463f61957d2b3046bcdf855e346601e4c7760c0ca618ee7bf55381" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "17b7f6bdfc1993c56dd9bd674cc276a55a46fdd9fd5fe435b9e4b7ebc7052a9dc76a99e4e43aba7d486603189c90d10a21ad3722c86bf5bc856a0f930ff5bca65be708b76bb8a29105da67f31eebcec81f28aaf526d2f8f0feac393a24959dcd612e2b93b4463f61957d2b3046bcdf855e346601e4c7760c0ca618ee7bf55381" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240104_0)
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
        
            key_len = unhexify( key_str, "7e19e400872eed721d560202cd757d3eb99729496b6e3a6d38dd8afe1066045a" );
            pt_len = unhexify( src_str, "3fb9abc7aba654dfb174e8899c17db222ffbb387b7260fc6f015b54f1cd74284c516e21aae3b72338e5e8dc643cfafca0678f5bda3a7539f1612dddb04366031b5a3eda55f3232c1b176cc9be7cc07e0ebca674a272224929c401a2530efc6d4eed0087b544b12d172a01bc8340d9c2a2ebcb5af8b07d96073a879fda140c196" );
            iv_len = unhexify( iv_str, "d2b277f78e98f1fa16f977ce72ee22a7" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "4c81c044101f458fdfac9ca3b9" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240104_1)
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
        
            key_len = unhexify( key_str, "d0653934a16fd36c27d54488a1829302b931bed6e26ca26047242b85b50bfb61" );
            pt_len = unhexify( src_str, "c02347e1add9178d830d8baaad9aeee37e958bedf2cc846e2561fe8c83481d0a8a85911e7f1f6e444b28f30bd96c13c390e80f616feb6844ee6fa486543a2e3f38c138f45b4405e3fb331b64648219aaf1d574be948ccfca6afc18d12488db19c35b05601e47c0af5d49a93a5dd4420f38585c1eb033e173376fa390d3f948df" );
            iv_len = unhexify( iv_str, "94886a1845aebba5ed6b86f580be47f9" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "4be34ff42085ef4443c8b6042d" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240104_2)
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
        
            key_len = unhexify( key_str, "d0f0ccb88c7cec9496f26a59ddc67dc59ebe49ae3dd89ef3be008598727e214c" );
            pt_len = unhexify( src_str, "7845e155f4f28021291e7c814a1ace8f42b239990831aa82758fc1e376cace0b6f668f7f2f224dede1ef5b1df7ae74b2c01483701044acbbb72a9216eec6b7ef0190f114b3c73c6985c4653f11601c774d10b7f9df1f1e1f3ff4fafa20d6525edb37d9e5acfafe6d3468ee068d407fdb56dc718c98425926831253978d727854" );
            iv_len = unhexify( iv_str, "e5ca84b907ac761a5e68a9080da0a88a" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "c8f78e4139dd3eaf2baef8aafb" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "0cc3ede50b0d3fb9ada11300a3239a383c98f968ad65266d57a195bb18d3e568fe6cabba258da4bee9e923c7c838e06dc887a6c49cc1453ea6a227c6a83e651a8742e0316cad5efc93739393e3603446b5c920a206db1434adbb8ebde4d1a7a8699c7f6c61b2d57c9709b564338423b4f526d6c157647a6c45da9dd521061f05" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "0cc3ede50b0d3fb9ada11300a3239a383c98f968ad65266d57a195bb18d3e568fe6cabba258da4bee9e923c7c838e06dc887a6c49cc1453ea6a227c6a83e651a8742e0316cad5efc93739393e3603446b5c920a206db1434adbb8ebde4d1a7a8699c7f6c61b2d57c9709b564338423b4f526d6c157647a6c45da9dd521061f05" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024096_0)
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
        
            key_len = unhexify( key_str, "e35dcea17cbf391491ae5ba6056d0dd13b348183474dd4b614742751bdebfc32" );
            pt_len = unhexify( src_str, "5213542beb044910d7fdeec8bb89de93f350760e493286eaef1140485380d429f74a4279c1842a5c64f3ca3381cb5dbb0621de48821bded650cb59703e0ca88f4e9c3d15875f9dc87d85ba7e4bae9986ef8c203fce6f0ce52c28e3a93befb4cc4ba3d963d2283cd30f9bf6ab99d92f2f4f3aff0b022f1751b89d43ea10bbb28a" );
            iv_len = unhexify( iv_str, "fa549b33b5a43d85f012929a4816297a" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "afa61e843cee615c97de42a7" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024096_1)
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
        
            key_len = unhexify( key_str, "844c50ddc0ac1d9364b21003287d6ae6360d12bbb17a85351362420ee4ca588e" );
            pt_len = unhexify( src_str, "3a3bf4ccaf05f7c02f5e158dd2c5cb08c6aed4b1ba404a6d8ef9a0737fe2f350b3e22188fc330ea63e35df82f996e3cf94d331c4246cdb25bb2c409762e05ddc21f337edee51b64f1766ad18f520b3f34735b24278d9d647c533a743e0c1e9c81e9dee975cdc47e8582113fd250ef59353605b64acb7c025a97854c1a5c03237" );
            iv_len = unhexify( iv_str, "2f8512bb7e214db774a217a4615139e1" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "f1da1cebe00d80eb4e025feb" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024096_2)
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
        
            key_len = unhexify( key_str, "2aae1aa047a20ed2d6d8336d923864cee9404f924031ae327fbfe2d293e1d93c" );
            pt_len = unhexify( src_str, "8e5b6b9e4e7d01de9a919dd33c0c1eb94dcfebf28847c754c62c1c00642d9e96f15b5d28ad103ff6969be750aadfd02fc146935562c83ec459a932a2fd5fda32eb851e6cff33335abd5c2434ae4f5524d6bc74a38094ced360f4606a1a17096ff06604952c8ca94a9a6dc4a251e13b0e0c54bd8a6dff5f397a1eb1cf186fa518" );
            iv_len = unhexify( iv_str, "3da9af3567d70553ca3a9636f0b26470" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "e1026b3d15d261b2fb47632e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "58c52ea9f3b162511160eed1a68b6f52b3c4f5834af728de97a3d9e4ba337b29aad12636003cf5be9ffbeae0f383f7cf32f645a8f6fc5cdc1cde91c625c69a92bc434ed671e52a0044a48f3fce55cae49a7d065c2a72603a7efe58b5a7b18ac500d1a51420e820357e7a439b1c02198ebe3d4e62d5573a3aa5f40900a21e3b41" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "58c52ea9f3b162511160eed1a68b6f52b3c4f5834af728de97a3d9e4ba337b29aad12636003cf5be9ffbeae0f383f7cf32f645a8f6fc5cdc1cde91c625c69a92bc434ed671e52a0044a48f3fce55cae49a7d065c2a72603a7efe58b5a7b18ac500d1a51420e820357e7a439b1c02198ebe3d4e62d5573a3aa5f40900a21e3b41" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024064_0)
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
        
            key_len = unhexify( key_str, "f3d69208cb0d27474e9a231cd46eac7c1574fff950c48bbd1ba03fad16f563df" );
            pt_len = unhexify( src_str, "0d1f06eef5e8f2c81d1a73bb1dca93c22cfb6e40e9948bc75b0d84830fb9216330424f580b89050c3fb3f620eca8f9fd09fb86d2e8b3a0869c6022d8a705fc280d66fd16d3aba7395d6be4bed44145d51d42d56285f3675726d62d94c081364a6d440511de83a613c598b03078e2ec7648c6302defbbea66aafd33e1a4b1686c" );
            iv_len = unhexify( iv_str, "b957f05921d21f2192f587768dc12b4f" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "322374fbb192abbc" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024064_1)
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
        
            key_len = unhexify( key_str, "cb2cdeb17fa6bcb006c7fc60858a12a411804464458db351957e8caf42f1ee6c" );
            pt_len = unhexify( src_str, "296504131354b2c1928982f12d408ba2377f2d4bbe87e4c69f92a15bf6003910a43bda6c8929df66b3ab1d202a5258cad199f32f36cc30d2dc06199c2a52f7ccadad1fce50123c5f8434dec57cc60cc780263d7aace8f59cc8a6c54bddbaded3adb12ae2ee0bacf6a8da635ff85b51a4e8a1b3dc404863b90059de4ad0f158dd" );
            iv_len = unhexify( iv_str, "31bd7c971a6d330b566567ab19590545" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "efc5a1acf433aaa3" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024064_2)
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
        
            key_len = unhexify( key_str, "f94170790fadab3240df568197f9d6f6855afaed8d07eceeaa2380121872529f" );
            pt_len = unhexify( src_str, "ed231b78db082f652bc6310c396993b52de804a82464fa3fac602a1286535f59c67fc2b1b420c7321eb42b971edde24cd4cb9e75c843f2ac6fb8ecdad612d2e5049cf39327aa7a8d43ec821161c385f3fdc92284a764a5d1cbae886f07f93017f83a105bb7c3cc4fc51e2781516a2471b65c940ddae6b550ad37b35f53d7cc64" );
            iv_len = unhexify( iv_str, "2f9c0647a4af7f61ced45f28d45c43f1" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "ab74877a0b223e1c" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "1cb5ed0c10cee98ff8ecfa5a1b6592391bbd9f9b1dc1ff351e0af23920d546b5e27d62b94daabd32f7f96a2632dc9fd7c19bf55f3b9b7cd492e76f4d6b0f5b437c155c14a75e65bfc4120bef186da05e06a2fd3696f210292ee422ddbce6e63d99ee766b68363139438733c5e567177f72e52ef2df6a7dd33fc0376d12ec3005" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "1cb5ed0c10cee98ff8ecfa5a1b6592391bbd9f9b1dc1ff351e0af23920d546b5e27d62b94daabd32f7f96a2632dc9fd7c19bf55f3b9b7cd492e76f4d6b0f5b437c155c14a75e65bfc4120bef186da05e06a2fd3696f210292ee422ddbce6e63d99ee766b68363139438733c5e567177f72e52ef2df6a7dd33fc0376d12ec3005" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024032_0)
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
        
            key_len = unhexify( key_str, "797c0091ff8787fe7cd0427c02922620e7f6fb71c52ddcc03a9f25c89ba33490" );
            pt_len = unhexify( src_str, "2d3efc8900315c3691a8e3c9de3319d4deaf538fcf41aa0e295b861d0ac85baf56d149a6437747dd6976f44016e012b88de542fb8e5b9e4ad10c19deec4b7c0b69bc1b2e33d44a981ded66127dea354b072010b8dc24b85ed2ffeea3b9c0e931619dbbf22677691f0d54fc03eaa162e0ab0d760ad41021f67057c0d6ac19ca8f" );
            iv_len = unhexify( iv_str, "69d81c73008a6827a692fa636fbab8bb" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "be2dda5c" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024032_1)
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
        
            key_len = unhexify( key_str, "90ce1afb5500489b9edbad987f4009509c847b3e55cdf0c764ef2fb085e3d033" );
            pt_len = unhexify( src_str, "98482b54edce2bac1cd64d44917dcf117ebfbfe26ad17a9b263447028304f1cf5a69559c05b5d833420f4fddb6e308277d01eb4b3235f1c4b47d33d3899325b55e7be19d43187a5b1b1354ce02a529b3df1c13b4883902ae9fc565079dee825e705f3e580371e4fd86c3b0d31bae98adb529901f346ca07127314152b4370edd" );
            iv_len = unhexify( iv_str, "e119e166471ecf44bc3a070639619931" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "b2f54b3a" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024032_2)
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
        
            key_len = unhexify( key_str, "29264a90f114a800c0fc3247b3bda00981a12a8f85cf3a19ea4c7ffdd005f4bb" );
            pt_len = unhexify( src_str, "587c8e53ab5ae8c31e16160b4a41d88798e27f4ad61c573c023c62d4dbb3952eef5026ad7b453fa9e0694347ab8fe50a6cf20da566202b81e325cee9c07ab2d4d53ed45b3ec2d2135936515f8a24f2a8116807dce9df3c44edf64c32647145152ff241d9e018e4101e400af070192dc3b498b5a213d265b4cfc8c8d4d7deccb5" );
            iv_len = unhexify( iv_str, "cf296aa43cb7b328e09c8975e067404e" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "56015c1e" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024128_0)
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
        
            key_len = unhexify( key_str, "84ff9a8772815b929d55f6052c0354cf3e02bcc8336fcfe5794952b4c45d5d96" );
            pt_len = unhexify( src_str, "a87de56d49725a1625baf12fd15931fe1a6783dce5d1e744eba108f45e0c105d8141dc027d0e33ad7efb6752b43729715e2f3e2c42ebdab4d5f72f886bd821c4372244699ddded99a63dbe7763a5a3bc21cbfc253cdc2514eba2a4f54e24dca7c207cb3f6ae80153d77fe0641f357d5a073dcd425c38deb77c45f27427345516" );
            iv_len = unhexify( iv_str, "5c044a66e488b853baf479f7dee2aadb" );
            add_len = unhexify( add_str, "00304e3d40cbc6d2bee0778462884f4ec047a8c74bb3dd7e100f2b9d0e529fd24730063986117b56ca876b208a3691425ac63afc3d504ccb499c76622eade09717023fcb7d956b01ce24a3e53cb5da472be3fcf5b278b5d9e377de22fab75bc74afa9670f5fe9691aa0ed77e43f6abc67a61ec409ec39fd66ac0307bf195f36f" );
            unhexify( tag_str, "72ddd9966ede9b684bc981cbb2113313" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "aadb8537309940422f67ca393aa6182d67fe7c52092538a15e98a4254f0a9087c7f10903d5e78078c2e55de914dec8b6b35cb720e3e55963c0ac9901e44b83a0e7c5b2d3f002aec0a4a08354febe47b2abb955f2a21107626ef0b8e1e099650812a6fecf36908fce2d078c2735cf7c2b970a309e5c6d6ff29c26a05720c57105" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "aadb8537309940422f67ca393aa6182d67fe7c52092538a15e98a4254f0a9087c7f10903d5e78078c2e55de914dec8b6b35cb720e3e55963c0ac9901e44b83a0e7c5b2d3f002aec0a4a08354febe47b2abb955f2a21107626ef0b8e1e099650812a6fecf36908fce2d078c2735cf7c2b970a309e5c6d6ff29c26a05720c57105" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024128_1)
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
        
            key_len = unhexify( key_str, "b5ca3991d0160b1729ae1a622dcf4b03b1f4ba86150bd66bf35cbbee9258af10" );
            pt_len = unhexify( src_str, "62aad5854a238f096bdde0711ac6f5763e7fea29db068ea8c911f17ba91e6d7807883e6fc5ba7db17af33da2b00973008a3425e65cc786ce1b97360019ee2cef74563d54752be436b905705b507c3d62689df4edf0356d26b693eb43d8a2a927a9f3866b7e0e19e84a90447bd6f47e31070fa7c2a71e3f78229ee19fa47e848f" );
            iv_len = unhexify( iv_str, "f8402184d1cc36df07b68ecb1ab42047" );
            add_len = unhexify( add_str, "d378cfd29758bcbd21e26a324239c42c992941b3ad68d9f2b3d2def3a051fd172ee882562970ef59798ff8d9eb5f724ff17626156f4cf5d93e41ffef6e525919af6194ea9bbb58c67563d3ffd90e5a6e2a3a33bd1fa3d55eff5dba7cd439d571f7e08014c4780e3d10904ef22b660897e78258da20b2600e88d71c35ecb6329a" );
            unhexify( tag_str, "9e8b59b4971130557aa84ec3ac7e4133" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "556dd32edc0af3c64186fe8c000ddad1516cd14721c93c228e379d4f87e32c79e734539cec930322048f34a2b34931c585d44f09966caf187ec4b9244c991a8a5f263e9da1d08d6086e52535afdb36c7662307521cbceb9ecb470a76970243723fbc1613b6ebbcae261ac2f1936e66ce29ec7350b2e6b2f73a910ade645154f7" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "556dd32edc0af3c64186fe8c000ddad1516cd14721c93c228e379d4f87e32c79e734539cec930322048f34a2b34931c585d44f09966caf187ec4b9244c991a8a5f263e9da1d08d6086e52535afdb36c7662307521cbceb9ecb470a76970243723fbc1613b6ebbcae261ac2f1936e66ce29ec7350b2e6b2f73a910ade645154f7" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024128_2)
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
        
            key_len = unhexify( key_str, "df867d1dd8a287821a54479cab6f88636d2aca30e1bf01a5dffc735e17590356" );
            pt_len = unhexify( src_str, "6517272cac85d7f38902bcb4b96a0c59c4bdc46bfefa6ebacd7f2fb1629b87ca91de2ffefc42ce3cfd34dcbf01b3f7cadcea3f99e6addf35d36c51f2ceb1f85c1f56a04ec9c9fff60cd7fc238674992183ea3de72ef778561b906202b7b83fe6562a0bca9c1e0a18638e8685b998b4192f5120435809ad6e93a0422d00725262" );
            iv_len = unhexify( iv_str, "35019826c51dd1ef07ff915d9ac4ea96" );
            add_len = unhexify( add_str, "0375ed93f287eefe414ab2968844bd10148860c528dbf571a77aa74f98cc669a7fc317adc9f7cf2d80dda29b19db635b30a044399f3665b6176ed669146d28f5ada03b3d32d53fe46575a8afcd37f20386d9e36f7e090b4fefadfab7f008e02f1b5022c0eeb81d03443a276eae48c038ed173631687d2450b913b02c97243edb" );
            unhexify( tag_str, "e49beb083a9b008ae97a17e3825692f0" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "723be39bc13adbc48c861b07753f64fac1ae28fc8933acba888b6538721df0a8b91c040a26522fe0dbb7335d8f63d209e89f7cde23afa9ca3c584b336d63a91e07fdd8808b14c3214c96a202e665bbaaa34248ff30348f3d79c9f16e66ad6c5903305acd887a89b6244eb7c2d96e18b13a686de935bf3821444ee20f48678be5" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "723be39bc13adbc48c861b07753f64fac1ae28fc8933acba888b6538721df0a8b91c040a26522fe0dbb7335d8f63d209e89f7cde23afa9ca3c584b336d63a91e07fdd8808b14c3214c96a202e665bbaaa34248ff30348f3d79c9f16e66ad6c5903305acd887a89b6244eb7c2d96e18b13a686de935bf3821444ee20f48678be5" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024120_0)
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
        
            key_len = unhexify( key_str, "0e8e9ce6294b7fbc534a96bdd060120976a6e08315d2ea73ac61d085cd462a44" );
            pt_len = unhexify( src_str, "9855f186b51358f0e2111c06bfaaeaec9bf95c55e246375c614fad9883d86c82a20c86538dc5f42a0ea69677d59a20c5112d15d2a8396f12096242ad5d7b838d16ee0679fc4017af75bc15e8ad2f77b0e802c864031cbfb0bacd95c828d1db4b7bab0713619e9e5e8fe6902aac7a9e6c42eb05f5b156f7e663ee43e6fdb62480" );
            iv_len = unhexify( iv_str, "4edc6be20f904b4789e5bee0a80a3fc8" );
            add_len = unhexify( add_str, "db28ce076b360816cd1e04b7729f8ab080e0a07f35204350f3bd056945aab8638c0e8311ab056f3e5debdbfbb03fae700770264faf73e0f3a05a5812aee84ab613c82f4a76da276250675f6a663f85e2c26d4f4a8666a7f4cedaffc1a7218dec11ca4e72b8b5d5b620d1efbd3d3b94a5ae0d118b9860dfd543b04c78d13a94c3" );
            unhexify( tag_str, "03cfe6c36c3f54b3188a6ef3866b84" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "e10142f852a0d680c983aad2b4609ccbd35ff61bb3eb66442aee6e01d4cc1cd70f45210acbd506395d6ca0cfebc195a196c94b94fc2afb9ffa3b1714653e07e048804746955e2070e1e96bff58f9bc56f3862aaa5fe23a6a57b5e764666ddec9e3e5a6af063f2c150889268619d0128b3b5562d27070e58e41aadd471d92d07e" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "e10142f852a0d680c983aad2b4609ccbd35ff61bb3eb66442aee6e01d4cc1cd70f45210acbd506395d6ca0cfebc195a196c94b94fc2afb9ffa3b1714653e07e048804746955e2070e1e96bff58f9bc56f3862aaa5fe23a6a57b5e764666ddec9e3e5a6af063f2c150889268619d0128b3b5562d27070e58e41aadd471d92d07e" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024120_1)
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
        
            key_len = unhexify( key_str, "886c77b80f5f3a21c01932685a540b23629f6d41d5574fc527227ed0bdf2e21b" );
            pt_len = unhexify( src_str, "53a17d7b69f607f08676d6f6dd4e8db08e01333a8355d8c87616e84cdf10ef5b041fc6ddc3f6a245c0f534c2b167064af82f45e4702a5e8dede59579fdecf6713353392433950c9b97c38d9ee515ac97d0970ccf03981954540088567a30941bb2cca08cbed680500f8342faa7aebbc6c143e2ea57ba6b4ac1fd975dcc5d0871" );
            iv_len = unhexify( iv_str, "5ec506edb1890a5a63b464490450d419" );
            add_len = unhexify( add_str, "05b8d820c9f439d7aeae5c7da0ee25fb0dad47cc3e6f3a47e8b984e856201546975f8214531fc3c2e504d2ac10fa49cb948596b9a8fab01b95c49d6f04d1589f93b77b899e803dd20e1f00a51c0b5953e85be639109b14b100e35ca26d84ea629964b0db8260dfa5a150a66261bf37e79de2ec49e9f1b082a7c58ecd3d39b6c9" );
            unhexify( tag_str, "ffdf56e1c1a7252b88422787536484" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "79ee27adfa9698a97d217c5010ec807806feda37db811e398c3b82abf698aece08561fffc6c601d2691738e279eeb57e5804e1405a9913830e3ba0d7b979213ef40d733a19497d4bb1b8b2c609a8f904e29771fa230c39a48ebb8c3376f07c8013fff6e34f10fe53988a6ec87a9296c0a7cfba769adefe599ec6671012965973" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "79ee27adfa9698a97d217c5010ec807806feda37db811e398c3b82abf698aece08561fffc6c601d2691738e279eeb57e5804e1405a9913830e3ba0d7b979213ef40d733a19497d4bb1b8b2c609a8f904e29771fa230c39a48ebb8c3376f07c8013fff6e34f10fe53988a6ec87a9296c0a7cfba769adefe599ec6671012965973" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024120_2)
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
        
            key_len = unhexify( key_str, "5231ca6d772edd9ea2d251e22d7d455928c22474b4b44130dad57e6511fed6ee" );
            pt_len = unhexify( src_str, "2767c808410ee132291585ea74a48ad3102f883f07d060c91c5f10abd37fe0996d2210dc490260238ae15f5d74c7be2a1e15d80db09079c520047f88488a7802857a3fc3b81d85a96949997430a880177880a31d4d0c9c9045247804f057a4f2756d6e40375a4a3187c4376d6bf573ce334cda1ed88d8a50db499e7cdb89d8db" );
            iv_len = unhexify( iv_str, "048698a4a0feabc1f336112e2794795a" );
            add_len = unhexify( add_str, "3a81b6b0b722899ff931cb73c39222d555b83ae3f8880b982593cbc1ab8be90d1ee32fd7dfe697cf24c95b7309d82c3fed3aa6b3d5740cc86a28174ac8f17d860ebb251ac0d71751c2ff47b48bfb0b3beb4f51494464cda34feaecddb1dbbe5fa36c681ada0787d6ed728afc4008b95929a1905787917adc95f1034fedcd817a" );
            unhexify( tag_str, "ba61edeb7b8966188854fc7926aad2" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024112_0)
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
        
            key_len = unhexify( key_str, "5a3f516a7898e04e5da4efd6c7c5989b77552d195464620c2b35b9a4fda29cce" );
            pt_len = unhexify( src_str, "5cc28b61ae97557774bdcd7ff653f4aa349df68d53c7e5a65263883ef1fe224ad40e86bffc2d38f28a2ed9ae1fc08563e2a1e46246106546eb8e6064c06baa0046fa137421734b7f0f94656a4f459d9d981717557d843700d116b6e5e2dd3af5f67c34edf31b40b71fd3c6f2475f9310feb70bcb973be52d41e86792c49d54c0" );
            iv_len = unhexify( iv_str, "9310af6974890c0a0364231f9cc8103d" );
            add_len = unhexify( add_str, "2103af8356bcb9dfc2a4f1d4ed09cbcd8e1990d23865605e19f87feb50bf8d10d0257740e5557a9297f0499c01e29a1a513ca18e6f43f7406c865cbe3951a7771128f3110c8da3bd696368901944549552842a1f6fd96cc681b45da098f3c1acb3d237d2363285f520d0b6714b698790b7660c52ac84a42c9721ac7e9d38a2ef" );
            unhexify( tag_str, "993fc8e7176557ee9eb8dd944691" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024112_1)
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
        
            key_len = unhexify( key_str, "59c9258554363d8a885fc0f5d112fee08eadfc7ce52a0e7e73e3d0d41d9a0290" );
            pt_len = unhexify( src_str, "79c491411402ea7878e480519fd984dde44bce6459303bb76d4eaf97d4e345d1aafaa68ceb0590b41cfed0f411b675d9344c7e888cccfc9eb6fe6b229d198f94ba516ee850ee7f078a4f5f32a23f92f72264e3a76a31ebd042564315ac4f2ec0bb49ba6d08cfd2d3a6308688e39f28e3ecd669c588368cee8210edf5dbefb925" );
            iv_len = unhexify( iv_str, "77e51e89dc47bbcac79cca21e81a61de" );
            add_len = unhexify( add_str, "25a6f8800a9b914c0ebf9a45d72355c03ee72a138eb81b2980f332645ce1d7aa4659805821866aee2b276e2c032776b4eaf36f93b5f9a72b791be24e31eff105ca6d0700e3069ee327983dd7fe1c7465d6c6d77837aff69055149988e7199847fad98605c377d997dbd40f3e2ff1a4f978a493684e401249e69540fbde96323c" );
            unhexify( tag_str, "ee6d85d3f3703b45adb4f9b2f155" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "44ca68deed5478074adfddc97f06f44c08bf7bca4dee8707d621fc7396fe2efcdad0a167d1708a9ff59ce4cddb86920bf1dbdf41b2109a1815ffc4e596787319114cad8adab46cf7f080c9ef20bcf67a8441ba55eac449f979280319524c74cf247818a8c5478ea6f6770996026a43781285dd89c36212050afc88faa56135fb" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "44ca68deed5478074adfddc97f06f44c08bf7bca4dee8707d621fc7396fe2efcdad0a167d1708a9ff59ce4cddb86920bf1dbdf41b2109a1815ffc4e596787319114cad8adab46cf7f080c9ef20bcf67a8441ba55eac449f979280319524c74cf247818a8c5478ea6f6770996026a43781285dd89c36212050afc88faa56135fb" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024112_2)
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
        
            key_len = unhexify( key_str, "5e9eae594cb54c8089330e4404ff79abb1c0841b0be5347a14633ad1e1ff44fa" );
            pt_len = unhexify( src_str, "32abc1eb6077555a85a0a6fd1c78cccca6c8b375842e2eb8eee45ee6c38dc0837443d16c647252e8124639dd01c808ac5e857a25d927c2a75e2fa8955cad5beb5c206fc050cd933fc4621f5718936f01f39dd700ae1aee7537cc595df8789c5d1a6e1e87b1c7a60e3ce5d57c80dd65dee3801798e1481b1963bcc78cc69f8c50" );
            iv_len = unhexify( iv_str, "0917b486da754f48bb43ecc8766a7ce3" );
            add_len = unhexify( add_str, "2aa1ef2f91aeba5da10b48a882dbd4574df4e9157a18abf8cecd03e4176712ba171b6ecb0e745841ff84e35063e47b08101afc44cfd9cededb913a82f00b9d4bac922f23a22f200642270399896405d00fa5271718eefb4cd5fe7e5f32097766ebff36ff1898a1c8a1a01cc18e6121e470805c37ff298fc65ef2fb1b336d09fd" );
            unhexify( tag_str, "92282b022e393924ab9c65b258c2" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024104_0)
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
        
            key_len = unhexify( key_str, "aaf03c3055a35362212b9b059931e7a24fc71e32bc9a533428c9dc31077f2ebc" );
            pt_len = unhexify( src_str, "c0e12cdd8233878505e025d52427536be7b6bf1887d2dd20eac7092db80b22417a3a4ca83cdf5bc5e36161be1ff9b73f7ceb297c6d07c9cb2a75035a5dc079e48283daea60596f4b356ca28c243e628cbe459f069709fe193394c9b1a31d8ccc5a3a4eba30056c415e68571a2c34bb5c32efff12e9aa483c4a68be5e76aba4cd" );
            iv_len = unhexify( iv_str, "7dfccd077b29e6ed5720244bb76bde9f" );
            add_len = unhexify( add_str, "21edd1c6056f51fd5f314e5c26728182edcd9df92877f30498949098dcde8089eed84e76d774ef8874d77125669a302d268b99dcd66b349d0271dde6f8cc94dc4f2df3787887b1173cad94d067e346846befb108005387102854d9387d2c0fbc9636cdf73a10d145f4b612c201b46e1ff4465f6a7654ce3da5792daf9a27fb35" );
            unhexify( tag_str, "6154c6799ad7cdc2d89801943a" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024104_1)
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
        
            key_len = unhexify( key_str, "60c775971a9eac7950ed2bdd85bd60fe948ba04c419f6743fb67f37557e46c6e" );
            pt_len = unhexify( src_str, "8abb2e66a4d08074916056bb8e925551372f737f0e1b597c5d08ee102989743a273b29d7281013f8b3aee2934399cb427370d70370ee86eb41584b653660c633506a53cae747826bb7d93909f069d5aacf058b7f2bbdc58ea08653db857bda83a979fc22a4f126dfef7aac45177f4cdb802fab0c812fb35d12a8176ec21336d7" );
            iv_len = unhexify( iv_str, "9b92ad7079b0de09c94091386577338b" );
            add_len = unhexify( add_str, "1f6a84b0df75bd99a2a64849e9686957c6a60932ebe898d033128be9b757e9890225925d856bfdc33ff514c63145f357730bb0435c65342bc5e025267b410af6fd388a5eca01b7efc87fd3b1b791df791bd47dfab736350d7b7f368b4100e04c939d5af957bab95ed502dac904e969876674602a0f0790da2d7351b686e46590" );
            unhexify( tag_str, "1d6cd4ab3914e109f22668867f" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024104_2)
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
        
            key_len = unhexify( key_str, "3b426e449337a14bc0663246ab61b671b523c9a3130c21ed59c93fa6a5aa5ae3" );
            pt_len = unhexify( src_str, "291bd5a00d71eb7d547b7c94e7030ba4a947418eaeb378a3bacd304b08c6f92f6958eaba968ac6aa23e0512a2a8ad7c1ca2f8fcf623bfc1281f5b7b598c08d2aebcd447668b23238c5e338b4c2ac7f8fd381714c596ea3e0c17aca4317a08563e58f0f52a8af08e078dc242ae54ee0fe3869f8c9687b004a4ded0aa27d8f4c5d" );
            iv_len = unhexify( iv_str, "e6efc96acd105fe4a48d1ac931eea096" );
            add_len = unhexify( add_str, "0902cf7a0685444126369712ac47962bc2f7a3a5837f1b6190d9ab1adb4cd35e7f0892eee628b8e07fcf2b598cebe1ec07d8c4823172ae66a135bb51cc71590707b691a66b56af1ffe38772911d11685da355728eaddd83752d21c119d7b59f4c17c2403629fa55cd70cd331aed7b0de673c85f25c2e9e0267f53f0b7480c8ca" );
            unhexify( tag_str, "ca4bfeedcd19d301d3f08cb729" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "bcef3f2fd101b828d36cb38530cf9a0a7a285ac1c55ee1069cc78466327e85887534c98a8891d579effd832c0f7d6e7e822fb1eea85a39317a547591def4aeed6660872859fc9d1df9725d3c40e9ccaa900e0f1426a55d20ac4f2e8e07bd3bbc687f8e059ab93e7604c97e75ac94be1c8c24f4c4da0080a4d77953fb090cbb62" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "bcef3f2fd101b828d36cb38530cf9a0a7a285ac1c55ee1069cc78466327e85887534c98a8891d579effd832c0f7d6e7e822fb1eea85a39317a547591def4aeed6660872859fc9d1df9725d3c40e9ccaa900e0f1426a55d20ac4f2e8e07bd3bbc687f8e059ab93e7604c97e75ac94be1c8c24f4c4da0080a4d77953fb090cbb62" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102496_0)
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
        
            key_len = unhexify( key_str, "ceaf204ff504ea8e7fade1a2097f2b527a44766860447322fa5ad346cd810217" );
            pt_len = unhexify( src_str, "1c8e4cf6018211518494d46c2e0607fa42e236abc28d58f8175c530f84b1f030572f5f6a74cb5517e1fb999a637d352afcbeadea9121e695675859b66b499a3a351ecba5226e58ebbb59fe12e359e4c89cd51c8703d4643c49921ae495801c73627df404b91e828e1d0e03ae09a39defb5aa5f2c8106953772ba0713d3261329" );
            iv_len = unhexify( iv_str, "cfdb8183251f4b61c64e73243594fdc6" );
            add_len = unhexify( add_str, "a60f3969fd1b14793dd1425aa0b1f742a4861e0b50eaffd1525cd209ba6d1252176763bb5bee59aaa55f92341cdc0705899aba44cf0ec05cbf80274ebef65cd9507fd4224b25cac19610968d6a37e2daf9ddf046ef158ef512401f8fd0e4f95662eebdee09dd4a7894cc8c409be086d41280bd78d6bc04c35a4e8cd3a2e83be3" );
            unhexify( tag_str, "9e45029f4f13a4767ee05cec" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "5cdc66b587ed5eebb04f42b83a6ab7017093514881c598cce332d74fa3fab927493ac15bff26835296e080b5b45ef907c0529fc2f4ed2fc09db179ef598e5d193ea60c301d3f8d823404814e3e74de0e1d2417c963e9246c353201c7a42659d447376e7d05c579dd4c3ae51c2436407b8eff16ec31f592f04b8013efcfd0f367" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "5cdc66b587ed5eebb04f42b83a6ab7017093514881c598cce332d74fa3fab927493ac15bff26835296e080b5b45ef907c0529fc2f4ed2fc09db179ef598e5d193ea60c301d3f8d823404814e3e74de0e1d2417c963e9246c353201c7a42659d447376e7d05c579dd4c3ae51c2436407b8eff16ec31f592f04b8013efcfd0f367" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102496_1)
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
        
            key_len = unhexify( key_str, "15652abe38cd09777bba21d0db04637f5737d3cb3922181b9f2d07bfdafd327a" );
            pt_len = unhexify( src_str, "1d6c153dec3b4738a09c9fbdfe31a093eb7ea79b8fa49f83e5e1f46893590f074fb171fb66e30ef887767014e3a10a3aa05da2bd50dd7b7936e1d7f6f31af9030e31e76bdf147f4396464db0f6a72511c4885c6c2305d339906e3c761a3249d7ebea3bf463e8b79c3706e684575550e964b8047979f7aed6ea05056c4b5840b1" );
            iv_len = unhexify( iv_str, "3a5e0d223ae981efb405566264e3e776" );
            add_len = unhexify( add_str, "cd755437cb61b539908e0cfaaa36c0123f8f17d1e6539783cb61d4b56cac3bc1e971c1ea558b12669b025cb6b9ad55991c6e2f8ee8b0b7901790193e226a0fbbfff7ff0bee6a554660b9f32e061b6c04bf048484ff9ebd492f7e50e744edd72d02c8fd32f87f9421bf18a5a20ebb4d9dbe39a13c34b7296232470e8be587ba09" );
            unhexify( tag_str, "01a573d8e99c884563310954" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "162430c23f7adcf98575a2d9249b4b5cec42efae33776360ebfa6a19c8eee4bd6b07cbd274deadc3292b7cdbb7803e99d9f67ccc5077f3ad5808f339a05b3213dbfd11377673d4f9b486a67a72a9ac8ea9ba699861dce0de7e2fd83d3ba2a2ec7fabf18b95a2bbe2184ff7bddd63111b560b3afe7f2c76807614ba36c1b011fb" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "162430c23f7adcf98575a2d9249b4b5cec42efae33776360ebfa6a19c8eee4bd6b07cbd274deadc3292b7cdbb7803e99d9f67ccc5077f3ad5808f339a05b3213dbfd11377673d4f9b486a67a72a9ac8ea9ba699861dce0de7e2fd83d3ba2a2ec7fabf18b95a2bbe2184ff7bddd63111b560b3afe7f2c76807614ba36c1b011fb" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102496_2)
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
        
            key_len = unhexify( key_str, "a43f6d07042a15cd49f6f52a2a3a67c6c2ff420d95bb94b9fe03b287c3abcaf8" );
            pt_len = unhexify( src_str, "b67e58c8b608724fd20aa097ee483bc4c804490cc79de635170944af75c87ae0ad8261365c1dc80d852553bcba18da9fbc3fbe61d27550a03003ef0c60202054626655509a9e1ab54677e537a4e761df011d6c6dd041c795446b384161ae9eab441afd24d19b58eb4fe5116cd7b11b751ebbd0a2adba7afc380d9d775177099a" );
            iv_len = unhexify( iv_str, "3b6fad21f0034bba8b1f7a344edf7a3c" );
            add_len = unhexify( add_str, "2e01c0523c8293fc51388281dccdb8d0a2d215d729289deb327b8142d716c2bb849e9476545b82f3882ba7961b70c5da2a925ba18b6b121e9215d52ac479c9129c9cd28f81584ff84509d5f9dcb7eaae66911b303cc388efa5020ac26a9cd9ea953f61992a306eb4b35bcd8447eea63cef37bb0c95c1e37811115cf26c53e8c5" );
            unhexify( tag_str, "43470bc3d7c573cb3a5230f5" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "e1720d451fa7ab9db4988567187244b15b6fe795dd4fef579fb72e41b21aaa436d2e5d8735a4abd232a3fb9188c75c247f6034cdebb07fd7f260f8e54efefa4f2981cafa510dd5c482a27753a7c015b3cae1c18c7c99a6d6daa4781b80f18bbe6620bfc1518a32531017a1a52aadb96a7794887c11ad6bdd68187ba14f72a4b5" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "e1720d451fa7ab9db4988567187244b15b6fe795dd4fef579fb72e41b21aaa436d2e5d8735a4abd232a3fb9188c75c247f6034cdebb07fd7f260f8e54efefa4f2981cafa510dd5c482a27753a7c015b3cae1c18c7c99a6d6daa4781b80f18bbe6620bfc1518a32531017a1a52aadb96a7794887c11ad6bdd68187ba14f72a4b5" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102464_0)
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
        
            key_len = unhexify( key_str, "1f0f0191e18db07c0501dbab4ed952c5603a4cd249d2d8d17e62e10b96ae713f" );
            pt_len = unhexify( src_str, "aad40e7866c26e486b6f6e8eb14a130d5f88891bf0d09aa8fe32f447ab8dea7bee5d3eda4499c0103a010483f2b64fdf1155499d31decf528c77dd7627884f9995c213cf7402143dbb7561d69c86886734260ac94ffac7eb33598d25714228ef43f744ec1af2a87e789f1e5d6fff0fbd5082dcc49328f194e8f8a14a5bfc962d" );
            iv_len = unhexify( iv_str, "ab8be16b4db809c81be4684b726c05ab" );
            add_len = unhexify( add_str, "a5a6e828352a44bd438ad58de80011be0408d410f6e762e3145f8b264a70c593476b41bb87875746c97de7d5fab120bd2f716b37c343608ee48d197a46c7546fafcdbe3e7688b7e9d2f5b6319c91d3881d804546b5f3dbe480996968dd046f406c11f0dc671be0421cbc8b4ea6811dd504281518bb96148dddf9f0dc4e2e2436" );
            unhexify( tag_str, "d8bd7d8773893519" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102464_1)
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
        
            key_len = unhexify( key_str, "a6cf7d83137f57f2310ee6bf31e8883952bb07ccdc12f516233ed533ea967e5d" );
            pt_len = unhexify( src_str, "83ab20698fd7573fd121976a72b45a7f03aad84702fc8ac73d6926eabd8a546895aeffe4ba81d117507e2cd37d58eeff71cc3afa8a4449be85f228ea52f6dc6395bb43c1c9f795343720841682d9b2f00602eafa4d4cbe297bfc62467e526b9d823cc8eeecd9e5f8dbc2f65610663c6f37b3d896651b254bd60215629ade3b2a" );
            iv_len = unhexify( iv_str, "f17e37e73a28c682366bfe619cc673bb" );
            add_len = unhexify( add_str, "0f4dd201b18e20230b6233e0d7add6f96537dd4e82d3d0704c047fab41af5faf6bd52bd14fa9a072f81d92a2ce04352f0b66f088c67102d2d127a9850b09ff6087f194a6e8ccaba24091feb303eebb65f1203b2d22af44e7be4de71f03e6f6cbadf28e15af58f58eb62e5bddfae06df773cc3f0942520de20078dda752e3270f" );
            unhexify( tag_str, "74110471ccd75912" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102464_2)
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
        
            key_len = unhexify( key_str, "b0c85ac6b3887639838ddca94c5c69f38115aa00122322c8114642d12ea1b8fe" );
            pt_len = unhexify( src_str, "0210fce418e7e2199cb8f899c81b9be74a630d00269755f882fc4db27632e99685cc12c426a7503473646df1288d0ede28408be9add5713628700f8e2b2e27d7522520ed00ac47239084651eb99e7d03e1520aae137b768f3144232c16b72158fd5da4a26a2525b9b27791bf06d1eb2e671c54daf64fddc1420bc2a30a324ba5" );
            iv_len = unhexify( iv_str, "14f68e533ecf02bceb9a504d452e78c7" );
            add_len = unhexify( add_str, "796a46236fd0ff6572b1d6257c874038f870aa71cbb06b39046d0fb6489d6ae8622b5154292ae5c4e1d5ff706daedb2e812533ae3a635d339a7fbe53780e3e8204924a5deb4b6856618f4c7465d125a3edffe1ab8f88b31d49537791c0f3171f08dbb5ed1d9ed863dafbae4ecb46824a4922862fe0954ee2caa09ab0e77ed8fc" );
            unhexify( tag_str, "6fb0b5c83b5212bf" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "5e6c362f7587936bcb306673713a6f1fb080783a20e9bbb906456973e529cfa0298206184509c30e1d3793eaaa5d564edd4488f04311821eb652e0a1f4adaf6971505ca014788c8ce085ceb3523d70284ed2bb0aebeba7af83d484df69c87f55a93b3d87baa43bd301c4e55eb8c45dcf3e4612535ea1bd5fdb4c3b9056d0cae9" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "5e6c362f7587936bcb306673713a6f1fb080783a20e9bbb906456973e529cfa0298206184509c30e1d3793eaaa5d564edd4488f04311821eb652e0a1f4adaf6971505ca014788c8ce085ceb3523d70284ed2bb0aebeba7af83d484df69c87f55a93b3d87baa43bd301c4e55eb8c45dcf3e4612535ea1bd5fdb4c3b9056d0cae9" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102432_0)
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
        
            key_len = unhexify( key_str, "e61b1a6b40e2ab1245ff65dcfb9948318ac4fe55e9ed600cec301dae32ae0e93" );
            pt_len = unhexify( src_str, "8d67fa9fcf078e421cb63abeb25dba739ab0e09a091dd06b0c616e1e888f350edb2d73a42f57f115266ea20c7f8fc143ac746649612df06a5e29b4a15934dc049be1ab49d018ab86c4f37d8c3d9c714f038029e74d8ee3dbe61d81adc63712ea413b37f7604da12107aa1695d9b0981e5a92cdfaa5fbda0e31b22c6fd6f3b499" );
            iv_len = unhexify( iv_str, "c356244b3034d288e4d4fe901b8e27c1" );
            add_len = unhexify( add_str, "bdcfeb09d5b97bab05a7acd9849e7de2c5beb7a4dc573c7e1c1d0c0409245a6584023114fdcc6413c800ca16847bde750b27c4d590248e2ce457c19b0f614f6aff4d78d4a19b3251531e5e852fbb05d09412cc1ff8988d1955ca6f5fe2d820f20a7642e3ae69e8122b06ba0918e806400b9b615e1abe6fdd4f56a7d02d649083" );
            unhexify( tag_str, "86acc02f" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "7c73182eca97d9617abb478a6ce62e3491a7e9951981c89c3071b161a4c80440614c3f24d0155073e28dcccee96bc8303dab4901ef77318df522d16d9da47770ef022395d6104cd623d93d67090a27507fc8ca04157e7939e639c62cd0e7d8a472314833c0eaa9ba2fd54a25b02854e3bff25cccd638885c082374ae520ed392" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "7c73182eca97d9617abb478a6ce62e3491a7e9951981c89c3071b161a4c80440614c3f24d0155073e28dcccee96bc8303dab4901ef77318df522d16d9da47770ef022395d6104cd623d93d67090a27507fc8ca04157e7939e639c62cd0e7d8a472314833c0eaa9ba2fd54a25b02854e3bff25cccd638885c082374ae520ed392" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102432_1)
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
        
            key_len = unhexify( key_str, "4f5a02e9843d28c8c226ed70d44b8fced8fb757ab6ece4d4f06e3c3cec79e44f" );
            pt_len = unhexify( src_str, "3ec13950d329f24074714c583bdc35686b811f775b76b0a8fcfa66fc56426c9d022f8ab0af38f8d2f71a068548330cdbe891670181ed7491bf40c739ef4dd93689fd35929b225089d2b151f83d9b3cd767300611144586767354c0491112c205409f3168092d27f9b9f433afb79820a2811984d48e70c1fb2a13bbb3ddbc53fb" );
            iv_len = unhexify( iv_str, "099e5d9aae89fb6391a18adf844a758e" );
            add_len = unhexify( add_str, "ad93e8662c3196e48cfdb5aa3bc923cd204151aa980cbec78f0d592b701f779c1c49f9e8686d7e2385a4146b21a643a59c18c8b82214f42560bcd686fad7c7c8e8c1944ce6b20ec9537dd14b6cf2592740ca112f4cd582250d69f240d3e957040e1f7e19c60b3c8f2bd00cb666604c38946eb9b2f17336d281b4794f71e538a2" );
            unhexify( tag_str, "30298885" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102432_2)
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
        
            key_len = unhexify( key_str, "1cdb218e0bd0e02156e5b48182990f778889793ef6018a8928e61164ac047c8e" );
            pt_len = unhexify( src_str, "4d039618a0eb640329f90fe97de18bc928fc3fc7a0db42c97774bec2e882e872fc1097c8319f7837a16516bf387b1bae321c565e8fc1cb8480f051158e4685f0adba310d2c6253bc1300403cbd3f7ddcb2796a69f8bf9e73d47aada9a02673c1a3d5ecdac838abf22b385906236529a1b7dd5b8af2611a04cf4f83b15ba41cfc" );
            iv_len = unhexify( iv_str, "d2ffbb176f86bee958e08e5c7c6357c7" );
            add_len = unhexify( add_str, "bc580c4223f34e4f867d97febf9b03629d1c00c73df94436852cafd1408c945c5474c554cb0faf2bae35d3160c823d339a64ebd607cf765fa91f416fc6db042bc2bd7445c129b4a0e04b6f92a7b7b669eb70be9f9b2569e774db7cb7ae83943e3a12d29221356e08e5bf1b09e65f193d00d9fe89f82b84b3b8b062e649163dc8" );
            unhexify( tag_str, "1997daa9" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800128_0)
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
        
            key_len = unhexify( key_str, "dc1a145c18bdbca760f35eea0d4a5992de04a0615964ec8b419c8288ab1470f0" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "7f8368254955e1b6d55b5c64458f3e66" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "8ddaa2c3ed09d53731834fa932d9d3af" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800128_1)
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
        
            key_len = unhexify( key_str, "7b4766d3a6615ee58b390daa228ae7a541c46ce80a1efe227cc43cb777df3232" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "274367f31ec16601fe87a8e35b7a22dd" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "5f3a757b596e06e9b246ed9bac9397f9" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800128_2)
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
        
            key_len = unhexify( key_str, "d19b04055bf6e7ff82e89daef66c9d8319ab25f9197e559444c5729b92c4f338" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "796efaff4f172bef78453d36a237cd36" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "3b445f38bf4db94f1a9ec771173a29e8" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800120_0)
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
        
            key_len = unhexify( key_str, "7ca68e300534a90a7a87ca9906e4ac614a6aa51f769b6e6129753a4f83d10317" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "45e6b23f8b3feefd4b0ea06880b2c324" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "6c0a1c9c2cf5a40407bfa1d5958612" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800120_1)
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
        
            key_len = unhexify( key_str, "a2b7cd693239bbc93599d3d12c9876e7303b227b8ae718e2c62e689e1fd62903" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "548c9c8fcc16416a9d2b35c29f0dacb3" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "3aa21f221266e7773eeba4440d1d01" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800120_2)
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
        
            key_len = unhexify( key_str, "156b854beb0c276a5e724f5da72f0d1ca4ae7cbd5f93a2257d95c2e5bfd78ad4" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "a5129e2530f47bcad42fc5774ee09fe7" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "6bb09ed183527c5d5ed46f568af35f" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800112_0)
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
        
            key_len = unhexify( key_str, "d824330c60141264e1f709d63227a9a731bcc42b4adec1d8f0161b10b4fdb2ab" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c5afaa45312c64ab3c3cf9d6c4e0cc47" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "55952a01eee29d8a1734bbdf3f8f" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800112_1)
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
        
            key_len = unhexify( key_str, "b5517589948d8aea778df6fd66c17a170d327f69e504f0a4bd504c4286a9f578" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "6404b111c6289eefa0d88ed6117bb730" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "637f82e592831531a8e877adfc2c" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800112_2)
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
        
            key_len = unhexify( key_str, "f6137b2bcbd327fbcc7f313efa10f6ffaed30e4782e222e1225c87103fcae905" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "3b87b08337a82272b192bd067e3245ec" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "1f2dda372f20ffddd9dd4810e05f" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800104_0)
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
        
            key_len = unhexify( key_str, "b5e70d1b78e931abf44bba3f937dbc344858516a8a8afe605818dc67d0c3e4c4" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "58e70095c6f3a0cda2cdc7775e2f383d" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "1763573f7dab8b46bc177e6147" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800104_1)
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
        
            key_len = unhexify( key_str, "90de0c047d1dd01d521f2dedec7eb81bc0ace7a5a693a7869eaafbb6e725ad7b" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "d565c9cdfb5d0a25c4083b51729626bd" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "78738d3e9f5e00b49635ac9a2d" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612800104_2)
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
        
            key_len = unhexify( key_str, "c43e8dbeafb079692483a9fcbab964b76fccca6ca99e1388a1aa9bf78dfd2f02" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "f2bd4fe0d30c0e8d429cac90c8a7b1c8" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "ea7b52490943380ccc902ca5ae" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280096_0)
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
        
            key_len = unhexify( key_str, "13540919fdb95559e37b535a427efeee334309e34c4608459e204d931b8087e7" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c993c1802df0f075ce92963eb9bff9bd" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "edfab013213591beb53e6419" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280096_1)
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
        
            key_len = unhexify( key_str, "2a7b2e07c148ff0f627ae28c241a395876bbed0c20f3fd637330e986db025714" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "8f7e1621c2227839da4ea60548290ffa" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "f9da62f59c080160ec30b43d" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280096_2)
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
        
            key_len = unhexify( key_str, "b3e7837a75b38ae6d4299a1ae4af3c2460dfca558708de0874d6b1a5689b8360" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "05d363b2452beff4b47afb052ac3c973" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "6b4a16d1ea1c21b22bdcb235" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280064_0)
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
        
            key_len = unhexify( key_str, "9df3ccd95f7570f6ecf5e5329dcb79bcd46cbcf083fe03aa8f5bd0f645c6a607" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "774f4e70a7577b5101c0c3d019655d3e" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "98ff89a8e28c03fd" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280064_1)
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
        
            key_len = unhexify( key_str, "1c7123e2e8d3774c8f1bdbb2272f19129e04f29b4351ae19c3b9d24e6ea1fe87" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "99f25cebd6cfa7f41390b42df6a65f48" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "8e14a0a4853a156a" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280064_2)
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
        
            key_len = unhexify( key_str, "490090323e9257517e2453469caa3414045cacb4d05d5cebc6b9c06fa6d19291" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c1beff1ff6cdd62339aa21149c4da1e6" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "f998d7c08d609b3a" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280032_0)
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
        
            key_len = unhexify( key_str, "360e48dd38d9e7f5bf29a2994ab5b3c9c70247102d94049ae791850807a4c845" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "88126c350dfc079c569210ee44a0e31a" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "f2ebe5e4" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280032_1)
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
        
            key_len = unhexify( key_str, "1562b32e4dd843edaf4474b62cadd8f46d50461f5b22c9f1a8eae7367d35d71b" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "af29fdb96f726c76f76c473c873b9e08" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "13fd6dfd" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280032_2)
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
        
            key_len = unhexify( key_str, "d5160d0c98ffcb1c26aad755f67589000e2bb25fa940e6b1d81d780f421353d9" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "1552604763453b48a57cea1aed8113f4" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "660c5175" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024128_0)
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
        
            key_len = unhexify( key_str, "c3a3ea3a097c0c2b3a4cb78462d87fd5a8f348687c4150e9d3354b388ab13d17" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "f77945979241fb3a454d8e3da193e169" );
            add_len = unhexify( add_str, "a69bac31241a2c07d3f7e331b77f662b1e67ccb81c07f52578b01f5785de9437f02eb7627ca7b9af09c1cb428fe93d6deb31f4d6dd2f0729f87480bdeb92d985de1aaad4bcebc6fbad83bede9a5dd1ca6a15bf5d8a96d4edb5bee1f7d195e9b2e5fb2221a596d69f257c18a143eda870e22d3f2ed20c9b3b0d8c8a229c462fff" );
            unhexify( tag_str, "6b4b1a84f49befe3897d59ce85598a9f" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024128_1)
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
        
            key_len = unhexify( key_str, "e1626327d987342cba5c8c63b75b4ed65463a2b9c831f4f9f80325fa867d1d73" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "4e25800deab7ecec2a2311f8fb44eb7d" );
            add_len = unhexify( add_str, "ebaffd558f24dae03117c69ac4b2b4aaeaffe7e0e7599eaba678bfce23a9914dc9f80b69f4a1c837a5544cba08064a8f924064cba4d783623600d8b61837a08b4e0d4eb9218c29bc3edb8dd0e78c1534ab52331f949b09b25fbf73bece7054179817bc15b4e869c5df1af569c2b19cb6d060855be9a15f2cf497c168c4e683f2" );
            unhexify( tag_str, "8faa0ffb91311a1a2827b86fec01788d" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024128_2)
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
        
            key_len = unhexify( key_str, "938da64b837275b0c80c442bdf2301aa75e387fe65a775d10a8ec840f62ff429" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "dec6adeb60216cbb8a6c3afba49fa201" );
            add_len = unhexify( add_str, "4ac144bd95f405649444f01ab67ef3e4c0a54fdbd933b6ba00518c79db45c22c90030c45aadcfdb53ec8199be0cbb22dbb9ab938a871f4b3b0c98ed32590a051abb946c42726b3e9701f183b2092985e3457943a6350fbcaece2e6b111b179ea3fd10ac080a577a1481785111d5f294bc28519c470ff94392a51a2c40a42d8b5" );
            unhexify( tag_str, "2211ca91a809adb8cf55f001745c0563" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024120_0)
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
        
            key_len = unhexify( key_str, "e2436484ea1f454d6451ad8dbd1574b208d7a3ab4fa34869299b85c24348b43d" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "97040d2ec094fe1c64fa35b35b7451a7" );
            add_len = unhexify( add_str, "bc198677513ce0e66697dfe52b22315fa5d8f92042f34cc9f373a01f94607df1a599132f60af010ed9b5e52162dd7b162912b68b11700e08f5fdafd84d10f760fc05ec97c05b83e55155194f399594015b90a19c04fb992e228940fe1b54ba59c4bb8318b33cc0df1cb1d71c389473dfb3eefabfe269ca95db59a7bc0201c253" );
            unhexify( tag_str, "2e080ba16011e22a779da1922345c2" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024120_1)
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
        
            key_len = unhexify( key_str, "7fb3fc72eb8a3aa5b102f90039f852cc3fd64f46915f5e49f1d9e02fe9cc13b1" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "f6120fea313362524917c53d90bafb4f" );
            add_len = unhexify( add_str, "60c2be7fbd15faf895fd19a9ce775fe2b183b45cffafe4fcbf50d421bea97347e41a9418cfa129b2dda63b889a70063010215dbe38c37feae18bc31b34f31b726f22177f2b4b9d648dd4aa80edfd12dafaee10baa83224354432d1cb62ccabe38bb8448d162cd0d30e988d2e1a2458ffdafaacbdff928756390f66dc60d7ea45" );
            unhexify( tag_str, "83de3f521fcfdaff902386f359e683" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024120_2)
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
        
            key_len = unhexify( key_str, "697c96d80d0a3fa9af35b86f31fb71a17aed30ce841c79896bbc8863b3b3ee04" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "3a5163ec7e007061838d755ac219855e" );
            add_len = unhexify( add_str, "de50c12da63232768d5eb9920d49683b5b7114cb77448fa10b9d63552ec5d9c2eac94b375d11f944959f903bb20c696639b6e7f108ec1e873870098c631ddacb2c25268cfc26d2a4cacfb7dda7383374c5456bcf4daa887a887f4293f8caa14419472a8bf7ffd214dfb2743091238b6d1142b116c2b9f4360c6fe0015cd7de81" );
            unhexify( tag_str, "cd4542b26094a1c8e058648874f06f" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024112_0)
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
        
            key_len = unhexify( key_str, "66c1d9ce3feb0e966c33e3fd542ec11cc32f18c2514b953103d32abcdc72633a" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "46fdb88fdde9b7d74e893802a0303256" );
            add_len = unhexify( add_str, "55d2f263d2e3cf0b390fce1dd1ebd5f666086f26e1ce2f08002bedbb810ada3922c6bfcf6a6adaa556e9e326c9766f02b3eb6e278da2fa3baa7dbdb6373be3c6ecfbe646b1a39e27c5a449db9b559e7ea3496366b8cdbca00ee7a3dea7fdfbea1665bbf58bd69bb961c33a0fd7d37b580b6a82804f394f9d5d4366772cee3115" );
            unhexify( tag_str, "96ca402b16b0f2cd0cdff77935d3" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024112_1)
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
        
            key_len = unhexify( key_str, "d7c949420dc9497232cd5810f316d11f9e85d36c430b5943ba79836d88c1eb92" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "7ef9788ff09cbeedd9569d49083a4097" );
            add_len = unhexify( add_str, "ca1de5cc3fcde2638eb72210e551e9c0e0a3f5570d5be83a9a4406b545d854bf17e75b9cd0f4c45722fbd71319a317b72a8798485e9316a1c8102432b83bc95af42f6d50700ba68f6f2e19b6af609b73ad643dfa43da94be32cc09b024e087c120e4d2c20f96f8e9ddfe7eae186a540a22131cedfe556d1ebd9306684e345fd1" );
            unhexify( tag_str, "8233588fca3ad1698d07b25fa3c4" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024112_2)
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
        
            key_len = unhexify( key_str, "6fe7c70815aa12326cdcbb2d2d3e088bbaaef98b730f87fe8510b33d30e12afe" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "e0253bd1f19e99a7f8848206fb8ac4a4" );
            add_len = unhexify( add_str, "397897eca4856f90d14c3cdfe1ad3cba47e23174ae2dab7d2a6320898584e03bffa3ffd526f416d7b3c579b0f3628744e36eebb5df519240c81d8bbbf5c5966519c5da083ab30a7aa42deae6180e517cdd764b7f77d19cc1a84141817758887a8d7265e7e62279b9d33cd2f1ba10fd54c6c96d4b8a5dbe2318fef629c8e2af0f" );
            unhexify( tag_str, "477b0a884d788d1905646bd66084" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024104_0)
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
        
            key_len = unhexify( key_str, "cbeefb3817cb02d617f385cf2371d52c8bcbc29e5e7a55cd2da131ca184c6e89" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "f74156d6400ae46b612531848bffe18f" );
            add_len = unhexify( add_str, "1abe2ab05ceccf2391273126fe4a4426b94d2c3b97a7f1cd2ee6bb952bf4a546e972b5a1701d5ddb0e5bb7a248fcb47107a9fc77e4b9806b68a11850119aa239fa8be1370e3a2e1a8b168f7323afdfc4b8917d92570167848a56132d68876abc386c258a9233dc8a9eb73443b052e842c3d63e8b5369acdd038404e4e9a4b038" );
            unhexify( tag_str, "0cb67cec1820339fa0552702dd" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024104_1)
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
        
            key_len = unhexify( key_str, "e6f5f65ce2fc8ec3f602f5df90eb7d506dd771337913680ac16bdcd15c56583d" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "9212a548c597677d1747e98ce6fb18a4" );
            add_len = unhexify( add_str, "55ca486c0183d0134925880d2e21dde0af51c4c77c6038a5a9c0497884e0aa4715bdb5b4bb864acc708ac00b511a24fa08496df6a0ca83259110e97a011b876e748a1d0eae2951ce7c22661a3e2ecf50633c50e3d26fa33c2319c139b288825b7aa5efbd133a5ce7483feecb11167099565e3131d5f0cb360f2174f46cb6b37c" );
            unhexify( tag_str, "08d7cc52d1637db2a43c399310" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612801024104_2)
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
        
            key_len = unhexify( key_str, "0e9a0391435acb57eae2e6217e0941c79a3ff938ec6a19b8a7db2ea972e49f54" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "27cd1d7af7e491e30c8110cc01392529" );
            add_len = unhexify( add_str, "79140d32bb32dace0779e2d37a0f744d6d973e99a279962b43a6c0af63772e8a0a21d5d9dd3c33d4b218cb2f6f24dd8d93bb4e1e6a788cb93135321ecfed455e747fa919b85b63b9e98b4980a8ccb3b19d50d735742cb5853720c2ad37fa5b0e655149583585830f8d799c0d2e67c0dc24fc9273d9730f3bb367c487a5f89a25" );
            unhexify( tag_str, "fbb477dd4b9898a9abc5a45c63" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102496_0)
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
        
            key_len = unhexify( key_str, "55a12eeca637654252e3e40b371667e3f308b00f2fd2af696223e4cd89e3fd4e" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "8a3793b6441258360f7f4801b03d0b26" );
            add_len = unhexify( add_str, "f5810dc5f25e49bd6d94bc63c2494aa7a579a4056a25f1dd9b2734d0b8731ee52523edd54ff475651d45c213e1bf254327fb0e2c41a7d85345b02bcc9d27b08915d332e1659671991a4bb74055967bebbba6ecceb182f57977130623d5a7b2175fa5a84b334868661c1f450b95562928b4791759796a177d59ed18bbf141e2ad" );
            unhexify( tag_str, "99230019630647aedebbb24b" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102496_1)
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
        
            key_len = unhexify( key_str, "3d353f870a9c088de5674efd97646b9c5420b2bcdfcffefcadd81682847e5331" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "f267fa982af5c85359b6447f9b7715ea" );
            add_len = unhexify( add_str, "7cf55630867af5dff747c8dd25bcc531d94a7730a20b6c03d46059ea93fcaa00d07ee17dad0e0dff814b02dfef0cbe00b37fd2f5f95ead7c72be60016f2934d7683fc1e47185c7211c49cb03e209b088edb14e533dbcb792ab7033728904f7ff12381a236dba97894ec1fafcf853ab15fff343f9265d0283acef10168ffd1271" );
            unhexify( tag_str, "9553b583d4f9a1a8946fe053" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102496_2)
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
        
            key_len = unhexify( key_str, "d227c9ff5d17a984983056fb96f3991932ae8132377529c29238cf7db94a359d" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "b8f6536f376a7efe0e684acf350bae70" );
            add_len = unhexify( add_str, "1cc25da31f90de7fa47ebce92754d3faa99f88d4e25ccab45645c1acdf850d55d7f02f61a0bfdc3125f29259d7da8abef532fe0966c63d3486753c8a2cb63a39349a0641b2f2b9526a03b97d58ca60fbb054c6c164ff2836688b0cad54df2b165bc082eeae660e768dde5130e30f8edc863446661c74da69b9e56de8ae388da0" );
            unhexify( tag_str, "44b95a37fab232c2efb11231" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102464_0)
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
        
            key_len = unhexify( key_str, "b2a57ef85ffcf0548c3d087012b336c46f6574cf1d97ca087bfad042ee83eec2" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "3d580402d2a8dc4d7466e5dcb456be7a" );
            add_len = unhexify( add_str, "c2b9e95c16e55028794a63ef82d11fb83a2a75dc34a81f238e472c33264534bdd54cd07d02a0ecf9019ad1a6d6c779f339dd479e37940486950f183bade24fca2f24f06d4037b3555b09fc80279ea311769473eb0630b694a29823324cdf780d7d1a50d89f7a23b05f7a8c3ad04b7949aa9e6a55978ba48d8078b5a2fd3c1bbb" );
            unhexify( tag_str, "072d4118e70cd5ab" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102464_1)
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
        
            key_len = unhexify( key_str, "63889ed5bf2c27d518a696b71c0f85592e3337aae95b5bf07289e4c5dfdc088d" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "1ad534280a0fac7dce31f2ae4fb73f5a" );
            add_len = unhexify( add_str, "be1b9dabea33bb9443e27f674b27931c0fba699a33dc86fab29e50b76a9441030444b465317bbf2949faf908bc1b501d11a5ea2042e4b460a85f3be5836729e523d99b56ef39231d5c6d8ae2c2ab36ef44e2aa02a1f2c559c6e333216c7f9ed5f9b880a88e920219204c99a3ae8f90afd1396563bc59a691a93e0070b0b5fd90" );
            unhexify( tag_str, "1bcea0ac2c1a0c73" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102464_2)
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
        
            key_len = unhexify( key_str, "94e3e2c17cfb6f52d4fdba3ba6d18bba891b6662e85df14d7e61f04adb69e0e5" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "8a80efb3bfe220526997543409fddb4d" );
            add_len = unhexify( add_str, "05da1b0f7ac6eef488d3f087ecae7f35abe3ef36d339709dc3fcb5b471979268ee894c3b6c7f984300d70bc5ea5fba923bfb41d88652bdaecc710964c51f3e2ae2c280b7d6c8e3b9a8a8991d19d92d46c8a158123187f19397ad1ad9080b4ffd04b82b5d68d89dacd3e76439013728c1395263e722b28e45dabf1ef46b8e70b5" );
            unhexify( tag_str, "faa5c13d899f17ea" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102432_0)
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
        
            key_len = unhexify( key_str, "fe5e479ad0d79dbf717a1f51f5250d467819e444b79cb3def1e0033c80ddadd8" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "47ce838083fd070d8544c0ad5337cdc6" );
            add_len = unhexify( add_str, "98476bf05a18c4ff1b6024dd779c1ac06d838705a0a83fe42bee5fc6ebf3b2a1a5049b67f4aabc8239cd6ff56504bcbad1e2498c159bbec2a6635933945f6ea49e5bc763dcf94f4b3643d3888f16105abb0965e24f51cb4949406124145e9ae31cc76535b4178492f38b311099df2751f674363ae7a58f6f93019653b7e6a6f0" );
            unhexify( tag_str, "a3958500" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102432_1)
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
        
            key_len = unhexify( key_str, "27d4dedb71a8f68ca5ce2b9e56da772bf5a09b7981d41cd29f485bd2d1adb8d4" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "7e6f0343c54539717a97b6c8b9f7dec4" );
            add_len = unhexify( add_str, "d386db78043f719b7e137cbf79a7f53dda2fe3baccbebb57d499f6eb168e5151f10081d76b72ae0f30165efbdda469e826f9246e59dbcad5c0b27691c00d6c192c24073e99c19cf8c142087c0b83c4ce2fc7ba1e696394e5620ab2d117d5dcd2ac2298997407fd5de07d008de8f9941a4a5f8074736a59404118afac0700be6c" );
            unhexify( tag_str, "50fd1798" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561280102432_2)
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
        
            key_len = unhexify( key_str, "5a7aa836a469d28542d0d24d3232fad266da8fc889c6b6038b726d3da25f7b20" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "9faf7cd805803e143ec8f3f13475efd2" );
            add_len = unhexify( add_str, "1006c707f608728b2bf64734062b12a5625062bcdcb80a3ce2058352a2922d5e6fbe19681b4f0d79ad3c837f81e72f2fbf8df669894e802a39072b26c286f4b05188c708f7c6edd5f5bb90b87ffa95b86d84d6c1c4591b11d22c772a8ad7f2fe6bd8b46be0e93672df2e8bff8ba80629e1846cfd4603e75f2d98874665c1a089" );
            unhexify( tag_str, "07764143" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240128_0)
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
        
            key_len = unhexify( key_str, "a9444fd176acbe061d0221fde3ddfcc4ff74e995d981a831297c4cbda51c22a1" );
            pt_len = unhexify( src_str, "c146ff5a988496cad7eced7a2ea471e0117d5d6bd2562c23ce9db4bf36d83ba3fc22e90486ec288a627d208e0b2fd3b65f8301cf7fc41d97959981a95cd1cf37effc46db99b94b21c941c3613c26a10b1a6b7793f467d58ff5134612230f1c49d7e1fcf664fe52fc6eca46273982f6fe729b009d90eb8d8e4a0b0dbe907b76da" );
            iv_len = unhexify( iv_str, "5714732145470da1c42452e10cd274b5" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "db85b830a03357f408587410ebafd10d" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "a3cad9a57fa28e6f6aaa37150a803bf8b77e765f0702e492c4e5ebb31ae6b12d791149153e469a92bb625784a699fd7ca517500ee3f2851840ba67063b28b481e24ba441314e8b7128f5aaccaf4c4e2c92258eb27310bf031422b7fc2f220f621d4c64837c9377222aced2411628018a409a744902c9e95c14b77d5bb7f5846b" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "a3cad9a57fa28e6f6aaa37150a803bf8b77e765f0702e492c4e5ebb31ae6b12d791149153e469a92bb625784a699fd7ca517500ee3f2851840ba67063b28b481e24ba441314e8b7128f5aaccaf4c4e2c92258eb27310bf031422b7fc2f220f621d4c64837c9377222aced2411628018a409a744902c9e95c14b77d5bb7f5846b" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240128_1)
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
        
            key_len = unhexify( key_str, "686d3bd071e3f46f180611bc4ec8d7726fe72b6c617e7d42b3339f53918c9e36" );
            pt_len = unhexify( src_str, "21983ad66449c557263aef299da6eef8f31d576fc17ed2dac3e836f7c2ceaff3094b2695452680e188df10c174810efd1fbaa6c832baedce0b92e4c7121447f6461ac909b4302cdf658095b1de532b536faa4fb38cfdf4192eb5c3fe090d979a343492f841b1edc6eb24b24bdcb90bbbe36d5f8409ce7d27194a7bb995ecc387" );
            iv_len = unhexify( iv_str, "a714e51e43aecfe2fda8f824ea1dc4b7" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "cd30c3618c10d57e9a4477b4a44c5c36" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "9610908a0eb2ee885981c9e512e1a55075a212d311073bbb2fb9248cce07af16ee4c58bdc8dbe806d28480f9065838146f3e1eb3ae97012cfe53863a13d487f061a49a6c78ca22a321fa25157dbe68c47d78f2359540cc9031ee42d78855ed90e6b8ea3d67725bfffcb6db3d438c982b5f88d9b660f7d82cb300c1fa1edebb6b" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "9610908a0eb2ee885981c9e512e1a55075a212d311073bbb2fb9248cce07af16ee4c58bdc8dbe806d28480f9065838146f3e1eb3ae97012cfe53863a13d487f061a49a6c78ca22a321fa25157dbe68c47d78f2359540cc9031ee42d78855ed90e6b8ea3d67725bfffcb6db3d438c982b5f88d9b660f7d82cb300c1fa1edebb6b" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240128_2)
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
        
            key_len = unhexify( key_str, "6fe81f15a02e2ecf46e61199c057102d160e6b5d447d4a275972323fff908c3e" );
            pt_len = unhexify( src_str, "0b4ee0385e6665da8fd2ae47f2d0cf1c5bd395a3bb447047ab5a3ae0b95355bf83d0381119a8d4c01acbe60cd7885da650502f73498a682fdc94f7b14f4c753226064fa15e3a90a6083e053f52f404b0d22394e243b187f913ee2c6bb16c3033f79d794852071970523a67467ce63c35390c163775de2be68b505a63f60245e8" );
            iv_len = unhexify( iv_str, "91d55cfdcdcd7d735d48100ff82227c3" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "cd7da82e890b6d7480c7186b2ea7e6f1" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240120_0)
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
        
            key_len = unhexify( key_str, "4c2095e1379389dc3810e8819314f5a2f87d1494213c5b1de1a402f7f4f746c4" );
            pt_len = unhexify( src_str, "26ec8ebac0560538a948afbc18fb730e9a91f21392bde24b88b200f96114b229a5b57fa9d02cf10e6592d4dfb28bf0f00740c61157ce28784e9066ea3afd44ecf3a494723610cb593c0feffc6897e3435c6f448697ad3e241685c4e133eff53bdd0fe44dd8a033cfb1e1ea37a493934eb5303ae6ef47ce6478f767ef9e3301ab" );
            iv_len = unhexify( iv_str, "19788b2e0bd757947596676436e22df1" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "f26a20bea561004267a0bfbf01674e" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240120_1)
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
        
            key_len = unhexify( key_str, "be5351efc0277afc9759ec2464a22cb4401f7a17efd1a205e7af023c7ed30ee1" );
            pt_len = unhexify( src_str, "1eca91406f338fc09c2988b1d7dc8c409d719300c03840a497d7b680cdd5e09b144903477f7116a934e1d931cf368af1fc2a0a0e7caa95475a3cd7bf585a16fda31eb3f8201db0216b37a1635c1c030836b3dd05ca5b0194388fa198e717822131d5d4318690ef82d35ac80b27fff19aec8f020dc6c6ce28f0813bbbf8230ad9" );
            iv_len = unhexify( iv_str, "c6b26117d9dbd80c1c242ad41abe2acc" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "61051d6c0801b4a6b6ca0124c019f3" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "95447aded336d6c20d483a6f062d533efed0261ad321d37bf8b7321b98f55c0f0082ce7f3d341b18fea29a72fc909d30cd8c84a1640227227287674a9b2f16a81b191ecf3b6232d656c32d7b38bea82a1b27d5897694a2be56d7e39aa1e725f326b91bad20455f58a94a545170cb43d13d4b91e1cee82abb6a6e0d95d4de0567" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "95447aded336d6c20d483a6f062d533efed0261ad321d37bf8b7321b98f55c0f0082ce7f3d341b18fea29a72fc909d30cd8c84a1640227227287674a9b2f16a81b191ecf3b6232d656c32d7b38bea82a1b27d5897694a2be56d7e39aa1e725f326b91bad20455f58a94a545170cb43d13d4b91e1cee82abb6a6e0d95d4de0567" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240120_2)
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
        
            key_len = unhexify( key_str, "814c2cdfdeecf39d43bb141fbfc62dac44f7552c5e5dac2d4913303fc860119b" );
            pt_len = unhexify( src_str, "0d3013a1d7132f685d001420daa6c7b643bc36b887511acc4588237d3b412c79e4ebba29c08248ad46c7239e8daa232b7483c9c4e3d1c0bbebc696401efe21f7fd6fc0525a4ab81bd9a893d5f7ab23b70ed07c00f33649b8a996a006de6c94f7793f72848793f4d5b31311c68aae1e715b37409fbe506dac038a0950f05fe82b" );
            iv_len = unhexify( iv_str, "0db3ade15cb0dea98a47d1377e034d63" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "e62f910b6046ba4e934d3cfc6e024c" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "374d03cfe4dacf668df5e703902cc784f011f418b43887702972dcc3f021bcb9bdd61ed5425f2975b6da7052c4859501eb2f295eb95d10ba6b2d74e7decc1acacebf8568e93a70a7f40be41ac38db6f751518c2f44a69c01c44745c51ad9a333eda9c89d001aa644f1e4063a8eb2a3592e21c6abc515b5aacaec8c32bcf1d3c4" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "374d03cfe4dacf668df5e703902cc784f011f418b43887702972dcc3f021bcb9bdd61ed5425f2975b6da7052c4859501eb2f295eb95d10ba6b2d74e7decc1acacebf8568e93a70a7f40be41ac38db6f751518c2f44a69c01c44745c51ad9a333eda9c89d001aa644f1e4063a8eb2a3592e21c6abc515b5aacaec8c32bcf1d3c4" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240112_0)
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
        
            key_len = unhexify( key_str, "1ae4541110f2bc4f83cd720b5c40c8315413d896e034b75007f172baa13d29ec" );
            pt_len = unhexify( src_str, "5ea811e7fbfc0e00bf2a6abfac50cad9efd90041c5f7fb8f046a0fecbd193b70a2de8a774d01dd3cd54f848cb3e9f5152ee1b052ba698bebfba1fbbdae44a260447d6e6482640ae4d01c9cac3d37d4ffe9a0de0b6001de504a33ef7620efe3ce48ecd6f5b1b3a89185c86d4d662a843ff730e040e3668d6170be4cced8a18a1c" );
            iv_len = unhexify( iv_str, "83f98eec51ee4cae4cb7fe28b64d1355" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "df47eef69ba2faab887aa8f48e4b" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240112_1)
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
        
            key_len = unhexify( key_str, "20c9b662ec4bd13bf58d64cb0a7159b0e7fee4703af66292bf75c8bd6e42e8dc" );
            pt_len = unhexify( src_str, "45b64f2ed5ac707890c0c1726adf338770ce6a728fe86bb372c4c49409a32705f881bc4d31a27c455c7c7df9dd2c541743523e7d32f88930d988857847f011be5f5f31a31e8812745147cbff5c1294d0fd4a7285db4833f22bf1975250da99c4d0dd2c9688d7f8001bb6ef2bc898ce4d42c5b78e74645b56ce992338f49d4183" );
            iv_len = unhexify( iv_str, "2bc0847d46f3d1064bbf8fe8567f54a2" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "5a1bf25aa8d5c3fe5cf1be8e54a1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "9079d6275db076625e8474c2914fe483d413d5339202f98f06c3b0ef063d8f3d31029deaf7f9349bfec57e5cf11f46f02d5a6520c7992efc951adbbea6d08e53faeb10dfe8b67ee4685da9ea4fe932551a65821147d06d4c462338e6ddda52017c2bc187fd6d02b7d5193f77da809d4e59a9061efad2f9cadbc4cd9b29728d32" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "9079d6275db076625e8474c2914fe483d413d5339202f98f06c3b0ef063d8f3d31029deaf7f9349bfec57e5cf11f46f02d5a6520c7992efc951adbbea6d08e53faeb10dfe8b67ee4685da9ea4fe932551a65821147d06d4c462338e6ddda52017c2bc187fd6d02b7d5193f77da809d4e59a9061efad2f9cadbc4cd9b29728d32" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240112_2)
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
        
            key_len = unhexify( key_str, "0a1554db37f2e275732a77e521cbd8170729d8677a85db73feacf3c66a89d689" );
            pt_len = unhexify( src_str, "5421d93b7e6e0091978c673df4f3a406aef5f13eb5e6f95da19b0783308cbe26d4fd6c669cc4a9f069d7e62e4c6fad14b80e918fe91556a9a941a28b3dbf776a68ac7c42df7059b5ed713e78120aec84e7b68e96226c2b5e11a994864ed61b122e7e42ef6cfdae278fadbae1b3ea3362f4e6dc68eef6a70477b8a3ffcfba0df9" );
            iv_len = unhexify( iv_str, "b9194a4d42b139f04c29178467955f1d" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "05949d591793ca52e679bfdf64f3" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240104_0)
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
        
            key_len = unhexify( key_str, "3ab1d9bb571c4bdc9f3ef340914bddcfe0c8e7718d4a2530334372cec86e5fcb" );
            pt_len = unhexify( src_str, "80bcea307e009745724d5f15d21f3b61a5d5a8401530346b34a2adfa13e3e8c9c9327d6fad914b081e554fbe6c1c6fe070b566620e559555c702c0ab5becf61ea1d9de64351ce43b2276ef4e20b5af7ce43db6d21286af4e740ef00c6d790705afcf0ee4850fffc12c662f2bd8212feb21db31065ab8f717a7509c213352b869" );
            iv_len = unhexify( iv_str, "6a5335901284dd3b64dc4a7f810bab96" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "04b8e5423aee8c06539f435edd" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "36b9602eee20b8f18dce0783cd1e01a799f81ae0a1ce6d293a26c62f47e7dad85c8446697cc09c81d3d9ead6f9e55c4147211660c8aea9536cc5516e9883c7d6854be580af8cd47ba38fa8451f0dad9c904e0e7f9997eff7e29bf880cd7cedd79493a0e299efe644046e4a46bf6645dfb2397b3a482a346b215deb778c9b7636" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "36b9602eee20b8f18dce0783cd1e01a799f81ae0a1ce6d293a26c62f47e7dad85c8446697cc09c81d3d9ead6f9e55c4147211660c8aea9536cc5516e9883c7d6854be580af8cd47ba38fa8451f0dad9c904e0e7f9997eff7e29bf880cd7cedd79493a0e299efe644046e4a46bf6645dfb2397b3a482a346b215deb778c9b7636" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240104_1)
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
        
            key_len = unhexify( key_str, "7dddbd5657e22750bfe6baa70a1f4ac46c1ef8bee573a57cfcef50b66f85e593" );
            pt_len = unhexify( src_str, "2bf5aba83a8161b9d21ff29251fb0efa697b1ea9c1b3de8481d5fd4d6b57afda0b098decdc8278cc855f25da4116ed558fc4e665a49a8fff3aef11115757a99c10b5a73b1f794f9502186c13dc79442f9226bbf4df19a6440281f76184933aeae438a25f85dbd0781e020a9f7e29fb8e517f597719e639cbd6061ea3b4b67fb0" );
            iv_len = unhexify( iv_str, "fcb962c39e4850efc8ffd43d9cd960a6" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "1d8cdadcf1872fb2b697e82ef6" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810240104_2)
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
        
            key_len = unhexify( key_str, "6916b93b2712421f1f4582de7ec4237c4e42e2b32c7dced2f8bb5bd2e0598312" );
            pt_len = unhexify( src_str, "3739cca20279a36ddb857ac22beae901a49529b3182463ab81a7c46e437eb0b0571e8c16f7b626ecd9f2ca0cd83debe3f83e5d58ed3738899f4b616755eb57fb965208f261736bdf7648b1f8595c6b6a779768115e3077dfee7a42d44b555a51675fb1ce9961d0e21b2b9b477c0541184350e70decf7c14a4c24b8a6cd5fed8e" );
            iv_len = unhexify( iv_str, "b4d9248bb500e40de99ca2a13e743f1c" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "090d03446d65adcc0a42387e8e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "0255be7ac7ac6feb3a21f572f6a593cc8a97f17af7064c80e478f4a6c469cf94d604bc014b003bf284d216161a9c8a493af43c6a0d8caf813a9e6f83c7ed56dd57543876b11f76aa2be80dcd79d19ac61f00fa423ac2f52fae7a8327cd91494ca4116feb735980ad0a4b1445cb7f38cc712b8aee72179e65b97fca38694e3670" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "0255be7ac7ac6feb3a21f572f6a593cc8a97f17af7064c80e478f4a6c469cf94d604bc014b003bf284d216161a9c8a493af43c6a0d8caf813a9e6f83c7ed56dd57543876b11f76aa2be80dcd79d19ac61f00fa423ac2f52fae7a8327cd91494ca4116feb735980ad0a4b1445cb7f38cc712b8aee72179e65b97fca38694e3670" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024096_0)
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
        
            key_len = unhexify( key_str, "b751c8b724165009a8bd97a9d2a0e22cae5a95c4743c55eeeef0a6fe7d946bec" );
            pt_len = unhexify( src_str, "e8546a5af1e38114822e60e75563a9399c88796f303c99c69d1f3c50379da81e1cd5b5a4a721e23c59da58ea4361b7ff58408e506a27fea24f9a235c6af7f7a5bd93fa31e90edfc322821c08d6324134830b7fe160b4a3e6d27866a10e6e60762a31618ef92f5c67ccb1deb1f1b188f0e687165e7c366c7418920df4f4fcdcae" );
            iv_len = unhexify( iv_str, "160c50c0621c03fd1572df6ba49f0d1e" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "9fef9becf21901496772996f" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "175fa6b7cd781ec057ff78ba410f2897a920739b5fc4f04bc9b998fbc7cc18e327ad44d59b167e4627256aaecd97dc3e4a7c9baaf51d177787a7f4a0a2d207a855753c4754d41348982d9418b6b24b590632d5115dc186b0ba3bec16b41fa47c0077c5d091ec705e554475024814c5167121dd224c544686398df3f33c210e82" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "175fa6b7cd781ec057ff78ba410f2897a920739b5fc4f04bc9b998fbc7cc18e327ad44d59b167e4627256aaecd97dc3e4a7c9baaf51d177787a7f4a0a2d207a855753c4754d41348982d9418b6b24b590632d5115dc186b0ba3bec16b41fa47c0077c5d091ec705e554475024814c5167121dd224c544686398df3f33c210e82" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024096_1)
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
        
            key_len = unhexify( key_str, "0faf32c22c2a4ee38fe4b5ce08f98fdf6f83b5038dcba5ec8332b3eeb5c710c7" );
            pt_len = unhexify( src_str, "8a556cc30075753c6e94c2f669bca2058ff6abcbffffc82da7cfca0a45af82dfb4cf487ceb4ede72be87ee4c8b72db1e96459de1dc96721464c544c001d785f2188b9fccaec4b1a37970d38b326f30163d2fdfdf8a2ce74aec55abcd823772b54f8081d086a2e7b17b4086d6c4a5ea67828ef0b593ea1387b2c61f5dfe8f2bb0" );
            iv_len = unhexify( iv_str, "04885a5846f5f75a760193de7f07853c" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "0c13506ed9f082dd08434342" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024096_2)
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
        
            key_len = unhexify( key_str, "0dddc3d2f82bdcdbc37648a6b9b416af28753740f8e998cd1a52a0b665369f1c" );
            pt_len = unhexify( src_str, "07bf84b15b21951fd22049be6991a672503ae243b8d285fb1e515e1d2c36bfd5b0d0bcce85791f2cea8f616aed68a7d9cf4eaf76418e8b1ec27751de67cbfd9d9f7905b2667904f10d598503f04c04ea00a681ff89a9c446d5763898430bd7a9dfebfe544e3ed3e639b362683a651e087626ffa63c0c2b3e0dd088b81b07f75e" );
            iv_len = unhexify( iv_str, "0a93b883cbd42998ae2e39aab342cb28" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "5c37918edb7aa65b246fd5a6" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "ff7b7b2f88b8c6f9f9bad7152874e995eea0ff1ce1ecd9b8d563642a37a31499f14d70f0dd835b7adf80928497f845fd8c2786cd53af25f8c9fe1bba24e3c3860162635bbed58f06cf6c9966bb9b570987a48329279bb84afb9e464bb4ad19ae6600175086e28929569027c5285d2ed97615e5a7dada40ba03c440861f524475" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "ff7b7b2f88b8c6f9f9bad7152874e995eea0ff1ce1ecd9b8d563642a37a31499f14d70f0dd835b7adf80928497f845fd8c2786cd53af25f8c9fe1bba24e3c3860162635bbed58f06cf6c9966bb9b570987a48329279bb84afb9e464bb4ad19ae6600175086e28929569027c5285d2ed97615e5a7dada40ba03c440861f524475" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024064_0)
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
        
            key_len = unhexify( key_str, "a0b1a62e46e7712277fc711e19d0c0c865ee77b42ac964b7202dbcaf428086c2" );
            pt_len = unhexify( src_str, "7dd7c0787fdbea4aacf929341659dcf4b75cbca8f92001e8b62a4d7b40272c5755fa9c445857db05328dc11ce5221f044f4b3dafbf0e2d72a1ad0d3e4c804148db578218690ccc620d8b97b4450ff83400a6caaa959617611446a6627138a4067be9ea410d4b0581022ab621928205b4a4480560fc4c2c3b39a2805684006f35" );
            iv_len = unhexify( iv_str, "e20957a49a27e247d00379850f934d6c" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "c99751516620bf89" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "9307620479f076c39f53965c87d20c2aff11c736c040dba74cd690d275591a5defc57a02f6806de82eb7051548589484364f6c9b91f233a87258ede1ee276cb2c93b4fc76f4d7e60cbd29ba2c54cb479c178fa462c1c2fb6eeb3f1df0edfb894c9222b994c4931dedf7c6e8ddecbde385ddf4481807f52322a47bf5ff7272991" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "9307620479f076c39f53965c87d20c2aff11c736c040dba74cd690d275591a5defc57a02f6806de82eb7051548589484364f6c9b91f233a87258ede1ee276cb2c93b4fc76f4d7e60cbd29ba2c54cb479c178fa462c1c2fb6eeb3f1df0edfb894c9222b994c4931dedf7c6e8ddecbde385ddf4481807f52322a47bf5ff7272991" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024064_1)
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
        
            key_len = unhexify( key_str, "ffcc1c88fba1723b3ab57b458d9bffb98b878c967fb43b9db2ae0753d32a3bb1" );
            pt_len = unhexify( src_str, "19b6dec86d93c466307de3a36c0791ed1010b1b9cf8d30347ae46e0f9283c9fda43da8cb491dd17cc4298b1f0b876d6a0f4bcbc9667fe34564bc08f8f7b67045057d19f4bf027bc839e590822fa09a5cef1af18e64a0116aa2a01a3f246c2b5272c18c9aa23efe674ba53d533ae8f0695cb78c1155cdc7a9d7fae2c4567dc07c" );
            iv_len = unhexify( iv_str, "d533c2170c5dc203512c81c34eff4077" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "167ec8675e7f9e12" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "0539287ac546fe5342e4c3c0ec07127dcd22899abfe8cdd6e89d08f1374d76e877bec4844d06e0a9f32d181c8d945ba16a54ce3725fae21d8245c070a4da0c646203d6b91325b665ab98c30295851c59265b4ab567b968b6e98536b7850738d92e9627b4c9c6f5d9ae2520944783d8f788a1aa11f3f5245660d41f388e26e0a1" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "0539287ac546fe5342e4c3c0ec07127dcd22899abfe8cdd6e89d08f1374d76e877bec4844d06e0a9f32d181c8d945ba16a54ce3725fae21d8245c070a4da0c646203d6b91325b665ab98c30295851c59265b4ab567b968b6e98536b7850738d92e9627b4c9c6f5d9ae2520944783d8f788a1aa11f3f5245660d41f388e26e0a1" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024064_2)
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
        
            key_len = unhexify( key_str, "55e94b339c3bafe068ef9cc30787cc6705850114976843777c92b4b331801650" );
            pt_len = unhexify( src_str, "147cc7bc4008dadf1956520b5998d961499bdf3d8b168591adbfd99411ad7b34eb4b2a5c1bb0522b810fec12dd7c775784d7ecdc741e6dec8191361e6abf473b219221801951b4d5ffe955ab50eef9cffdfee65ba29ddfa943fb52d722825338c307870a48a35f51db340aa946c71904d03174b1e4a498238b9d631a6982c68d" );
            iv_len = unhexify( iv_str, "2e2b31214d61276a54daf2ccb98baa36" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "5266e9c67c252164" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024032_0)
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
        
            key_len = unhexify( key_str, "13c9572bdef62510d84f2d415cc481cd1e71b9c1132b43e63b21ba4e16de9b39" );
            pt_len = unhexify( src_str, "7c78e634dec811173ff3c4a9a48ae3ae794fbd2aefd4b31701777ff6fcb670744c592a1d298d319717870dca364b2a3562a4ffa422bf7173c4f7ea9b0edf675e948f8370ffd0fd0d5703a9d33e8f9f375b8b641a1b1eecd1692ad1d461a68d97f91f9087f213aff23db1246ee16f403969c238f99eed894658277da23ced11ee" );
            iv_len = unhexify( iv_str, "a8339ba505a14786ad05edfe8cebb8d0" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "df3cab08" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "91f9780daefd2c1010c458054ac6e35baa885cdd2c95e28e13f84451064e31e0739f27bf259cb376ab951e1c7048e1252f0849ccb5453fc97b319666ebbfbc7ef3055212a61582d1b69158f3b1629950a41bc756bded20498492ebc49a1535d1bd915e59c49b87ffebea2f4ad4516ecdd63fa5afda9cce9dc730d6ab2757384a" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "91f9780daefd2c1010c458054ac6e35baa885cdd2c95e28e13f84451064e31e0739f27bf259cb376ab951e1c7048e1252f0849ccb5453fc97b319666ebbfbc7ef3055212a61582d1b69158f3b1629950a41bc756bded20498492ebc49a1535d1bd915e59c49b87ffebea2f4ad4516ecdd63fa5afda9cce9dc730d6ab2757384a" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024032_1)
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
        
            key_len = unhexify( key_str, "30a14ca53913acbb215b4e4159083106db3fff83cbedd1e5425f65af1e94f5dd" );
            pt_len = unhexify( src_str, "8c5f73ee1544553b712ad7a14f31379c8d54a4e432fb6c5112436988d83c4e94954b0249b470538fb977b756fbee70b811d4dc047a869e207bb0b495f1e271d0034e912000e97594033e0dedde0591b297f8a84bafcc93a46268a5bba117b558f1c73513e971c80a7083e1718fc12d0cc0d996a8e09603d564f0b8e81eea28bc" );
            iv_len = unhexify( iv_str, "4f23f04904de76d6decd4bd380ff56b1" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "18e92b96" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "bb4b3f8061edd6fa418dd71fe22eb0528547050b3bfbaa1c74e82148470d557499ce856de3e988384c0a73671bf370e560d8fda96dabe4728b5f72a6f9efd5023b07a96a631cafdf2c878b2567104c466f82b89f429915cf3331845febcff008558f836b4c12d53e94d363eae43a50fc6cb36f4ca183be92ca5f299704e2c8cf" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "bb4b3f8061edd6fa418dd71fe22eb0528547050b3bfbaa1c74e82148470d557499ce856de3e988384c0a73671bf370e560d8fda96dabe4728b5f72a6f9efd5023b07a96a631cafdf2c878b2567104c466f82b89f429915cf3331845febcff008558f836b4c12d53e94d363eae43a50fc6cb36f4ca183be92ca5f299704e2c8cf" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024032_2)
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
        
            key_len = unhexify( key_str, "e69f419140289ac25fb0e2ef9cc4f7e06777ac20f7d631918d1af0c8883b7d6a" );
            pt_len = unhexify( src_str, "ff8dfa4e70490ea9c84cb894dc5d7e1b935ebcdea80a39c4161d4db42cbb269cc86abd381af15ec9a4a42ed18c1eed540decec19722df46f22aa06883297cb393fb23e4bb31a817e88357aa923c7ecbcf24c28a09f622dd21fa70c0a02193024fdcefeaa96cc1b50f81a65dfa9e1bb5126f0c9766a861eed096ec15fb07b0f81" );
            iv_len = unhexify( iv_str, "531248afdaaf1b86cf34d2394900afd9" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "c6885cdd" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "f75299e0ead3834fc7ebd4b2051541b598ad57cc908fdcd4324cf4ccf7dcf7b3f0737ad6c026399a8b1b6d3d50011b3c48ea2c89833b4b44c437677f230b75d36848781d4af14546894eecd873a2b1c3d2fcdd676b10bd55112038c0fdaa7b5598fe4db273a1b6744cba47189b7e2a973651bfc2aaa9e9abea4494047b957a80" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "f75299e0ead3834fc7ebd4b2051541b598ad57cc908fdcd4324cf4ccf7dcf7b3f0737ad6c026399a8b1b6d3d50011b3c48ea2c89833b4b44c437677f230b75d36848781d4af14546894eecd873a2b1c3d2fcdd676b10bd55112038c0fdaa7b5598fe4db273a1b6744cba47189b7e2a973651bfc2aaa9e9abea4494047b957a80" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024128_0)
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
        
            key_len = unhexify( key_str, "404a5d1ac9e32f9caabffbfa485ce9c27edc9e5cde0f2aab4f32ce3121449b88" );
            pt_len = unhexify( src_str, "b63ec4d28854b7fe2d4d13973f5bcb16f78494ce25cc2820de9d0dc1d8d91db1f19bc9e01cee8418c9e88a69b2f30cdbb0dbdbb50be71e1e666c111c126f2b7197c02f69a1b2ec5e1bf4062b2d0b22fb0fa1585b4e6286b29f6ac98d1b1319dd99851fa6921607077d2947140fdeeea145b56ea7b6af276c9f65393bc43ede33" );
            iv_len = unhexify( iv_str, "b6e6c078e6869df156faa9ac32f057c3" );
            add_len = unhexify( add_str, "6ebc75fc9304f2b139abc7d3f68b253228009c503a08b7be77852da9e1afbe72c9ab374740b0dc391fa4d7e17de6a0aa08c69e6f5c5f05411e71e70c69dfbcf693df84c30f7a8e6c7949ea1e734297c0ea3df9b7e905faa6bbdcaf1ff2625a39363308331d74892cf531cb3f6d7db31bbe9a039fca87100367747024f68c5b77" );
            unhexify( tag_str, "94c1b9b70f9c48e7efd40ecab320c2d3" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "56a0ac94f3ec7be2608154f779c434ee96db5ed4f5a6e1acfb32361ce04e16e1337be5978df06d7c4f6012385fb9d45bb397dc00f165883714b4a5b2f72f69c018ffa6d4420ad1b772e94575f035ad203be3d34b5b789a99389f295b43f004de3daaef7fa918712d3a23ca44329595e08da190e3678bc6ad9b500b9f885abe23" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "56a0ac94f3ec7be2608154f779c434ee96db5ed4f5a6e1acfb32361ce04e16e1337be5978df06d7c4f6012385fb9d45bb397dc00f165883714b4a5b2f72f69c018ffa6d4420ad1b772e94575f035ad203be3d34b5b789a99389f295b43f004de3daaef7fa918712d3a23ca44329595e08da190e3678bc6ad9b500b9f885abe23" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024128_1)
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
        
            key_len = unhexify( key_str, "b56f0c980acf7875cf7f27d53ad4a276adc126d0b93a5774ac4277eecad4309e" );
            pt_len = unhexify( src_str, "2c94299e36b7c4a825ecbc5a7809061e0a6761764a5a655ffdb0c20e5c3fcb10f4e93c68aa0a38c2acc5d06f2b7c4ff4fcf814b551bfefa248dbe06a09a0f153213538a31fa7cf7d646b5b53908d8978f514c9c4d6d66f2b3738024b5f9c3fd86b6da0c818203183f4205f186ea44a54edb911b1a17c424c95852c8d271b2e93" );
            iv_len = unhexify( iv_str, "b004c049decfb43d6f3ec13c56f839ef" );
            add_len = unhexify( add_str, "b2045b97fbb52a5fc6ff03d74e59dd696f3f442c0b555add8e6d111f835df420f45e970c4b32a84f0c45ba3710b5cd574001862b073efa5c9c4bd50127b2ce72d2c736c5e2723956da5a0acb82041a609386d07b50551c1d1fa4678886bac54b0bd080cc5ef607dca2a0d6a1e71f0e3833678bf8560bc059dae370ec94d43af6" );
            unhexify( tag_str, "fce7234f7f76b5d502fd2b96fc9b1ce7" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024128_2)
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
        
            key_len = unhexify( key_str, "1c5027c36e6caa1b3e5e45fead32b5e3126ac41f106c491b0b3a7c16502f4fe6" );
            pt_len = unhexify( src_str, "58f0ceaa31c0025d2e6bb58720cce4b64f5f6c657c847ae42936eb1e343fea397c8a8cf2f5ef02ffaec25f431900dcb0910cf32cea9eca3b78aed1c451c7af51066489f87b2a5f8cf28d6fdb6ce49d898b6167b590a3907be7618be11fb0922a3cfd18e73efef19e5cdc250fa33f61e3940c6482ae35f339e8c0a85a17379a4e" );
            iv_len = unhexify( iv_str, "3ee660f03858669e557e3effdd7df6bd" );
            add_len = unhexify( add_str, "93e803c79de6ad652def62cf3cd34f9addc9dd1774967a0f69e1d28361eb2cacc177c63c07657389ce23bbe65d73e0460946d31be495424655c7724eac044cafafe1540fcbd4218921367054e43e3d21e0fa6a0da9f8b20c5cdbd019c944a2d2ee6aa6760ee1131e58fec9da30790f5a873e792098a82ddf18c3813611d9242a" );
            unhexify( tag_str, "ac33f5ffca9df4efc09271ff7a4f58e2" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024120_0)
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
        
            key_len = unhexify( key_str, "34c3019810d72b5e584f0758f2f5888a42729a33610aafa9824badade4136bbd" );
            pt_len = unhexify( src_str, "22deef66cbb7db240c399b6c83407f090d6999ba25e560b2087fed0467904bb5c40cbaa05b8bf0ff5a77c53fa229478d8e0736414daf9c420417c391c9a523fd85954533f1304d81359bdcc2c4ac90d9f5f8a67a517d7f05ba0409b718159baf11cd9154e815d5745179beb59954a45a8676a375d5af7fae4d0da05c4ea91a13" );
            iv_len = unhexify( iv_str, "f315ea36c17fc57dab3a2737d687cd4f" );
            add_len = unhexify( add_str, "f33c5a3a9e546ad5b35e4febf2ae557ca767b55d93bb3c1cf62d862d112dbd26f8fe2a3f54d347c1bc30029e55118bab2662b99b984b8b8e2d76831f94e48587de2709e32f16c26695f07e654b703eba6428f30070e23ed40b61d04dd1430e33c629117d945d9c0e4d36c79a8b8ab555d85083a898e7e7fbeb64a45cc3511d99" );
            unhexify( tag_str, "0bae9403888efb4d8ec97df604cd5d" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024120_1)
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
        
            key_len = unhexify( key_str, "29397d98fc5a7f04b5c8b6aa3a1dd975b6e4678457ae7f0691eee40b5397503a" );
            pt_len = unhexify( src_str, "0bbf1079cb5569c32257bc7e52371db46f3961b457402b816588243b4523543430d5ca56b52de6632724c51e6c3af310b28822c749a12bdd58dee58bbc3266631562a998ec3acdc8a2567a9f07f7f9759c3f50b1d1dcdd529256b80c0d227fc1fe8b58c62d1c643f1ac2996809fd061afcf4a9af184c14db9e63ec885c49de61" );
            iv_len = unhexify( iv_str, "885543a45fd1163e34ef9276145b0f8c" );
            add_len = unhexify( add_str, "d88beaa0664bcef178cbdbfab17ff526b5c0f8ad9543c6a312d93c336707fbf87c0448b07a550580953279f552f368225cc6971f1eecc718d6aad1729c8d8873081357752bd09d77075fa680cb2dc4139171e4a0aaa50b28c262c14fd10b8d799ca1c6641bb7dfdfdf3dea69aa2b9e4e4726dc18b0784afa4228e5ccb1eb2422" );
            unhexify( tag_str, "7b334d7af54b916821f6136e977a1f" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024120_2)
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
        
            key_len = unhexify( key_str, "7555dfcf354da07fd70f951d94ec1d86a635edfdb7929460207b2a39cc0cf4a3" );
            pt_len = unhexify( src_str, "a1351cfffd1b0cbf80c3318cc432d3238cb647e996b7b53c527783594683f535950cd08788687c77226b2d3f095955884adc2e475ca1e1eab04e37d5e901ae8934a9d3a0cb37b80612ca25d989856dfa7607b03039b64d7dcd468204f03e0f2c55cb41c5367c56ca6c561425992b40e2d4f380b3d8419f681e88ebe2d4bdad36" );
            iv_len = unhexify( iv_str, "e1b30b6a47e8c21228e41a21b1a004f0" );
            add_len = unhexify( add_str, "bf986d3842378440f8924bb7f117d1a86888a666915a93ba65d486d14c580501e736d3418cebee572439318b21b6e4e504a7b075b8c2300c014e87e04fa842b6a2a3ebd9e6134b9ddd78e0a696223b1dc775f3288a6a9569c64b4d8fc5e04f2047c70115f692d2c2cefe7488de42ff862d7c0f542e58d69f0f8c9bf67ef48aea" );
            unhexify( tag_str, "d8ef5438b7cf5dc11209a635ce1095" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "95e8db7c8ecab8a60ceb49726153a7c5553cf571bc40515944d833485e19bf33cb954e2555943778040165a6cfffecef79eb7d82fef5a2f136f004bb5e7c35ae827fac3da292a185b5b8fc262012c05caeda5453ede3303cfeb0c890db1facadaa2895bdbb33265ada0bb46030607b6cf94f86961178e2e2deeb53c63900f1ec" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "95e8db7c8ecab8a60ceb49726153a7c5553cf571bc40515944d833485e19bf33cb954e2555943778040165a6cfffecef79eb7d82fef5a2f136f004bb5e7c35ae827fac3da292a185b5b8fc262012c05caeda5453ede3303cfeb0c890db1facadaa2895bdbb33265ada0bb46030607b6cf94f86961178e2e2deeb53c63900f1ec" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024112_0)
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
        
            key_len = unhexify( key_str, "bbeafe86c72ab0354b733b69b09e4d3462feb1658fe404004d81503f3a6e132f" );
            pt_len = unhexify( src_str, "a033c2051e425d01d97d563572e42c5113860e5dedcd24c76e3e357559ba3250f1fc5d4a931a9d0900ac025400f0158621f0b1215b2907467bfc874bcabbb28e28de81fe1ee5b79985261c512afec2327c8c5957df90c9eb77950de4a4860b57a9e6e145ea15eb52da63f217f94a5c8e5fcb5d361b86e0e67637a450cdbcb06f" );
            iv_len = unhexify( iv_str, "ee1caba93cb549054ca29715a536393e" );
            add_len = unhexify( add_str, "e44b0e0d275ae7c38a7dc2f768e899c1c11a4c4cb5b5bd25cd2132e3ecbaa5a63654312603e1c5b393c0ce6253c55986ee45bb1daac78a26749d88928f9b9908690fc148a656b78e3595319432763efbcf6957c9b2150ccabfd4833d0dcee01758c5efb47321a948b379a2ec0abcd6b6cbf41a8883f0f5d5bf7b240cb35f0777" );
            unhexify( tag_str, "a4809e072f93deb7b77c52427095" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "e62adf9bbd92dd03cc5250251691f724c6ece1cb89d8c4daf31cc732a5420f6bedab71aab0238ba23bd7165ed1f692561ef457fd1d47413949405b6fc8e17922b17026d89d5830b383546ea516a56f3a1c45ec1251583ae880fa8985bd3dcc1d6a57b746971937bf370e76482238cc08c2c3b13258151e0a6475cc017f8a3d0e" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "e62adf9bbd92dd03cc5250251691f724c6ece1cb89d8c4daf31cc732a5420f6bedab71aab0238ba23bd7165ed1f692561ef457fd1d47413949405b6fc8e17922b17026d89d5830b383546ea516a56f3a1c45ec1251583ae880fa8985bd3dcc1d6a57b746971937bf370e76482238cc08c2c3b13258151e0a6475cc017f8a3d0e" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024112_1)
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
        
            key_len = unhexify( key_str, "6ad06c88dd4f3becf35eed95bb859be2406a1803a66e4332a74c5f75c09b9a01" );
            pt_len = unhexify( src_str, "2219c11672884b93d0290b6a7140feafe416461f1cdaf0b3aa64693d7db2eb10feae46aac7af549fa1b0abc78c11f8df7ee803ef70310fc3e67769f8b4bc64f81143a6ebf8bee9d386a8ede5d2cc0ed17985a3b7bb95191ef55e684690ccdc5ca504bc6eb28442b353861a034a43532c025f666e80be967a6b05b9dd3a91ff58" );
            iv_len = unhexify( iv_str, "07d8b4a6e77aef9018828b61e0fdf2a4" );
            add_len = unhexify( add_str, "cca1fd0278045dda80b847f0975b6cbf31e1910d2c99b4eb78c360d89133a1c52e66c5c3801824afc1f079d2b2b1c827199e83f680e59b9a7de9b15fa7b6848b5bf4e16a12ac1af4cf2b4d7bb45673c5e1241e9996440860a9204fc27cae46a991607bc5e7120d6c115ddcbdd02c022b262602139081e61eee4aba7193f13992" );
            unhexify( tag_str, "e3ede170386e76321a575c095966" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024112_2)
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
        
            key_len = unhexify( key_str, "87bbf7c15689e8c99a5a32a8ba0dfebcfe1989159807428cdd1f382c3ea95178" );
            pt_len = unhexify( src_str, "b77d3bf3b30b3e6e5c86cbfb7e5455f6480f423cc76834b4663d28d9f1eb5c40212634e3347668427f7848352ab789886f96682a568260bdaeb7de0aae2af36f5ae04f06c332b158d923706c1c6255c673feeadb6d30bfc901e60b92acd9ddd83ef98686c4d492f4a60e97af2541d470a6a6b21903441020ea7619cf28a06986" );
            iv_len = unhexify( iv_str, "2f19aa1f3a82a7398706953f01739da7" );
            add_len = unhexify( add_str, "590dbd230854aa2b5ac19fc3dc9453e5bb9637e47d97b92486a599bdafdfb27c3852e3d06a91429bb820eb12a5318ed8861ffe87d659c462ef167be22604facfa3afb601b2167989b9e3b2e5b59e7d07fda27ffccd450869d528410b0aff468f70cc10ef6723a74af6eebc1572c123a9b5a9aab748a31fa764716d3293ff5de7" );
            unhexify( tag_str, "5c43fc4dc959fabeebb188dbf3a5" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024104_0)
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
        
            key_len = unhexify( key_str, "24095a66b6eb0320ca75e2ab78e8496a45f4b000fc43436904c3e386fb852ed2" );
            pt_len = unhexify( src_str, "4690edc843e23d9d9b9a4dab8fa8193f8bf03897d3d29759e9dc9e0f8a970c0f5d4399b9f60461fe5cf439f9b0d54bbc075695e4d76b76298cc2b75bb3e0b516ee9ada93f77c4c002ba9fd163a1e4b377befb76c1e5ab8b3901f214c0a4c48bd2aa2f33560d46e2721a060d4671dc97633ff9bcd703bb0fbed9a4a2c259b53f3" );
            iv_len = unhexify( iv_str, "0955c1f0e271edca279e016074886f60" );
            add_len = unhexify( add_str, "f5160c75c449e6bb971e73b7d04ab9b9a85879f6eb2d67354af94a4f0ca339c0a03a5b9ede87a4ff6823b698113a38ae5327e6878c3ccc0e36d74fe07aa51c027c3b334812862bc660178f5d0f3e764c0b828a5e3f2e7d7a1185b7e79828304a7ad3ddcd724305484177e66f4f81e66afdc5bbee0ec174bff5eb3719482bd2d8" );
            unhexify( tag_str, "75a31347598f09fceeea6736fe" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "0dd2dca260325967267667ff3ccdc6d6b35648821a42090abba46282869bac4bdc20a8bee024bea18a07396c38dbb45d9481fedcc423a3928cfa78a2f0ae8eedb062add810bdbee77ddc26c29e4f9fda1ab336d04ef42947b05fbdb9bc4df79e37af951d19d6bf5e5cb34eef898f23642a9c4a9111ed0b7a08abeeefbbd45c23" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "0dd2dca260325967267667ff3ccdc6d6b35648821a42090abba46282869bac4bdc20a8bee024bea18a07396c38dbb45d9481fedcc423a3928cfa78a2f0ae8eedb062add810bdbee77ddc26c29e4f9fda1ab336d04ef42947b05fbdb9bc4df79e37af951d19d6bf5e5cb34eef898f23642a9c4a9111ed0b7a08abeeefbbd45c23" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024104_1)
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
        
            key_len = unhexify( key_str, "086b77b5731f971f0bf5b8227361b216746daf8b08c583ad38f114a64aa7877b" );
            pt_len = unhexify( src_str, "629317212ff8bd8a7676e4c00b81a9577de6397c832f99ac974fa2bbbccb6e3b8aa776db6922eed0b014bf3923799da7d9d0854c8817470e1e2f7fc7a572f9d0316ee60cde7ef025d59b897d29a6fee721aeb2f7bb44f9afb471e8a7b0b43a39b5497a3b4d6beb4b511f0cefa12ce5e6d843609d3e06999acfbee50a22ca1eee" );
            iv_len = unhexify( iv_str, "164058e5e425f9da40d22c9098a16204" );
            add_len = unhexify( add_str, "6633eae08a1df85f2d36e162f2d7ddd92b0c56b7477f3c6cdb9919d0e4b1e54ea7635c202dcf52d1c688afbbb15552adda32b4cd30aa462b367f02ded02e0d64eeee2a6b95462b191784143c25607fd08a23a2fbc75cf6bee294daf2042587fdd8fe3d22c3a242c624cf0a51a7c14db4f0f766ec437de4c83b64f23706a24437" );
            unhexify( tag_str, "2eb6eb6d516ed4cf1778b4e378" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_25612810241024104_2)
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
        
            key_len = unhexify( key_str, "0f9e806b0d937268561c0eafbbdd14ec715b7e9cef4118d6eb28abbb91266745" );
            pt_len = unhexify( src_str, "2ae4baef22ace26f464a9b0c75802303f2d7c0f9a1ed1d0180135189765bdd347fea0cc2b73ee7fbbf95ea1fda22597b8aad826f63e744069a9c349488b2cc1cf9372f423cc650302082125724730ae5a4d878e07385ddc99034c6b6b46748f02c80b179fe6406b1d33581950cb9bcd1d1ea1ec7b5becfd6c1f5b279412c433a" );
            iv_len = unhexify( iv_str, "8657996634e74d4689f292645f103a2e" );
            add_len = unhexify( add_str, "2ca253355e893e58cb1a900fbb62d61595de5c4186dc8a9129da3657a92b4a631bbdc3d5f86395385a9aa8557b67f886e3bb807620e558c93aea8e65826eadeb21544418ee40f5420c2d2b8270491be6fc2dcbfd12847fa350910dd615e9a1881bc2ced3b0ac3bde445b735e43c0c84f9d120ca5edd655779fc13c6f88b484f7" );
            unhexify( tag_str, "83155ebb1a42112dd1c474f37b" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "87d69fc3cbc757b2b57b180c6ba34db4e20dde19976bfb3d274d32e7cea13f0c7d9e840d59ce857718c985763b7639e448516ddbbda559457cd8cb364fa99addd5ba44ef45c11060d9be82b4ebe1f0711ac95433074649b6c08eeab539fdfc99c77498b420427e4d70e316111845793de1f67fb0d04e3389a8862f46f4582dc8" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "87d69fc3cbc757b2b57b180c6ba34db4e20dde19976bfb3d274d32e7cea13f0c7d9e840d59ce857718c985763b7639e448516ddbbda559457cd8cb364fa99addd5ba44ef45c11060d9be82b4ebe1f0711ac95433074649b6c08eeab539fdfc99c77498b420427e4d70e316111845793de1f67fb0d04e3389a8862f46f4582dc8" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102496_0)
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
        
            key_len = unhexify( key_str, "c24c17911f6db4b3e37c46bcc6fa35efc1a55f7754f0bb99f2eea93398116447" );
            pt_len = unhexify( src_str, "0bd92cb106867e25ad427ff6e5f384d2d0f432fc389852187fcc7b0bf9f6d11a102a872b99ed1ad9a05dab0f79fa634745535efed804ff42b0af8dad20ba44709391fb263f245e5a2c52d9ce904179633282f57a1229b0a9c4557a5c0aeda29bbc5a7a871fa8b62d58100c3722c21e51e3b3e913185235526e7a5a91c559717d" );
            iv_len = unhexify( iv_str, "5098cc52a69ee044197e2c000c2d4ab8" );
            add_len = unhexify( add_str, "9ad4dee311d854925fc7f10eca4f5dd4e6990cb2d4325da2ef25a9a23690f5c5590be285d33aaeba76506c59edec64b8c3ff8e62716d1c385fbce2a42bc7bd5d8e8584de1944543ab6f340c20911f8b7b3be1a1db18a4bb94119333339de95815cae09365b016edc184e11f3c5b851f1fa92b1b63cfa3872a127109c1294b677" );
            unhexify( tag_str, "f7930e3fab74a91cb6543e72" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "6124ede608d416baa5e653a898ca76e9f47f08403c1984feec112e670ded2226e0073f8881ab2161cfda541dccae19691285f7391a729f07aba18f340bb452c1da39cbe83cf476cfc105b64187e0d2227dd283dcba8b6a350f9956b18861fa131d3f00c034443e8f60e0fdfcfaabbed93381ae374a8bf66523d33646183e1379" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "6124ede608d416baa5e653a898ca76e9f47f08403c1984feec112e670ded2226e0073f8881ab2161cfda541dccae19691285f7391a729f07aba18f340bb452c1da39cbe83cf476cfc105b64187e0d2227dd283dcba8b6a350f9956b18861fa131d3f00c034443e8f60e0fdfcfaabbed93381ae374a8bf66523d33646183e1379" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102496_1)
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
        
            key_len = unhexify( key_str, "d267a8379260036ff3d1ec07a7b086ff75706bad12d37d9656f04776f3d8b85c" );
            pt_len = unhexify( src_str, "80c68a330ef50e3e516681f1e535868b03466e7edbb86cb385d01db487da3dd3edad940fdc98d918b7db9b59f8d61369eee2928c88557306c4a13e366af0708d94cb90a15f1c3bc45544bdb05ff964da5e06c5ae965f20adb504620aed7bce2e82f4e408d00219c15ef85fae1ff13fea53deb78afa5f2a50edbd622446e4a894" );
            iv_len = unhexify( iv_str, "674dc34e8c74c51fa42aacd625a1bd5b" );
            add_len = unhexify( add_str, "6a9a8af732ae96d0b5a9730ad792e296150d59770a20a3fdbbc2a3a035a88ac445d64f37d684e22003c214b771c1995719da72f3ed24a96618284dd414f0cac364640b23c680dc80492a435c8ec10add53b0d9e3374f1cf5bfc663e3528fa2f6209846421ea6f481b7ecf57714f7bc2527edc4e0466b13e750dd4d4c0cc0cdfc" );
            unhexify( tag_str, "bea660e963b08fc657741bc8" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102496_2)
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
        
            key_len = unhexify( key_str, "c86cb637753010f639fa3aa3bff7c28b74f012ad6090f2a31b0801d086f183ad" );
            pt_len = unhexify( src_str, "6b7858557e0fd0f957842fb30e8d54dedbc127eb4bbf9de319f731fa28a606df2c046a0bce8ecda4e75d3596e4e988efd6bc279aa005bc52fad92ba07f5b1dfda4cc417029f9778c88d6fe5341a0fd48893dcb7c68d0df310a060f2a5235aee422d380f7209bc0909b2aa7e876044056f0b915dab0bc13cbea5a3b86d40ca802" );
            iv_len = unhexify( iv_str, "87ff6e0bb313502fedf3d2696bff99b5" );
            add_len = unhexify( add_str, "2816f1132724f42e40deabab25e325b282f8c615a79e0c98c00d488ee56237537240234966565e46bfb0c50f2b10366d1589620e6e78bd90ade24d38a272f3fff53c09466aa2d3ef793d7f814a064b713821850a6e6a058f5139a1088347a9fa0f54e38abd51ddfc7ef040bf41d188f3f86c973551ced019812c1fc668649621" );
            unhexify( tag_str, "7859f047f32b51833333accf" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102464_0)
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
        
            key_len = unhexify( key_str, "2c31ca0cac3efe467168198f06beacf39565a6f57f82e1048a5c06a231315882" );
            pt_len = unhexify( src_str, "65261d6e29b2369b1828a7cef2df9873d6e6057c499301afedd6cb65b5036ddb95f9e353fbf38e54c4f46f88164325b33620ce183beb2e411fbb89a0e0002e542fc161cad32a61ee6f1e1717e0b4dcd0340b116f795bc1009dbbc65bc31c9b549bf03c40bc204cd0d02ec884be907777ebeed8b527ec3af7cbb508193c0745de" );
            iv_len = unhexify( iv_str, "95cae6e85f33f3043182460589be3639" );
            add_len = unhexify( add_str, "67523751a9b1b643d00de4511b55e4268cb2d18e79e01a55fc7b677d529bd6400940fb25ea6ae135c1a816e61b69e90b966981aeda685934b107066e1467db78973492ad791e20aef430db3a047447141def8be6e6a9a15089607c3af9368cdb11b7b5fbf90691505d0c33664766945d387904e7089b915a3c28886ba1763bb5" );
            unhexify( tag_str, "21309d0351cac45e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "1d5f2cb921f54aeb552b4304142facd49497837deb1f00d26fbeddbab922fd80b00dba782961f8fce84f1f7973e81eed6ee168b1760c575c891f40a1dae0fa1a08738025d13ef6e0b30be4f054d874f1b8a2427a19ebb071d98365c32316a88a68c2b40daf1ea831a64519ac3679acb4e04986ecc614ec673c498c6fee459e40" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "1d5f2cb921f54aeb552b4304142facd49497837deb1f00d26fbeddbab922fd80b00dba782961f8fce84f1f7973e81eed6ee168b1760c575c891f40a1dae0fa1a08738025d13ef6e0b30be4f054d874f1b8a2427a19ebb071d98365c32316a88a68c2b40daf1ea831a64519ac3679acb4e04986ecc614ec673c498c6fee459e40" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102464_1)
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
        
            key_len = unhexify( key_str, "ca9fa36ca2159dff9723f6cfdb13280446eb6bc3688043c7e2e2504184791596" );
            pt_len = unhexify( src_str, "ac04c4293554cd832aa400c811cb202d815d6178aa1343b4628592b7f3ae45dc5f12ea47be4b43e1865f40b06ab67b3a9fb3644248a9b3efe131a8addb7447978bb51ccf749e75574fea60e8781677200af023b2f8c415f4e6d8c575a9e374916d9ec3a612b16e37beb589444b588e0b770d9f8e818ad83f83aa4ecf386d17a7" );
            iv_len = unhexify( iv_str, "d13ca73365e57114fc698ee60ba0ad84" );
            add_len = unhexify( add_str, "2aa510b7f1620bfce90080e0e25f5468dbc5314b50914e793b5278369c51ac017eace9fd15127fca5a726ad9e67bdee5af298988d9a57ec4bbc43d4eb849535eb10521ac7cd7ed647479a42876af2ebc9e2108b539febdaa9127c49bda1bda800f6034050b8576e944311dfbca59d64d259571b6d2ed5b2fc07127239b03f4b7" );
            unhexify( tag_str, "2111d55d96a4d84d" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102464_2)
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
        
            key_len = unhexify( key_str, "2f802e838250064c15fdee28d7bd4872850355870847701ad9742b2d6eb4b0c0" );
            pt_len = unhexify( src_str, "e2ca8c8d172ff90232879f510d1225af91bc323bdf636363c2903fcd1790692c8bcb03a1cccb18814678852c6b3a441552e541b843ee5e4f86a152fa73d05aea659fe08aa6428bb257eaa2a7b579fdc4022c1dec359a854253c1aefc983c5ede8c97517ea69fc4606e25f13ffb0f5f49160691454fbb74e704326738353525f7" );
            iv_len = unhexify( iv_str, "2dd550cfd97f8e1d8d31ba5537ae4710" );
            add_len = unhexify( add_str, "72b9630dda40306e785b961934c56e20948f8eac0e981f49787eb3dbd6e4607f7d08d10ca643746bf1efa7e5066993683d527a90f2d45ec9cf73113f1f17bb67958be669acd4e2927f1dacfde902cd3048056d7f6dfdd8630ff054efce4526db7c9321d6d2be2236f4d60e27b89d8ec94f65a06dc0953c8c4533a51b6a29bd2c" );
            unhexify( tag_str, "bd6c8823c9005c85" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "f6dd0b5f3d1a393a1837112962dba175a13c2d1e525ef95734caf34949d8b2d63b4fe5603226b5f632f2d7f927361ba639dc0e3c63414f45462342695916d5792133b4a24c7c4cbe2b97c712bf27ab62d3d68b3875d58ffe4b7c30a8171bff1a9e2f3995768faacda2ea9213ff35798b9e4513f6a87bd3f5a9d93e847e768359" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "f6dd0b5f3d1a393a1837112962dba175a13c2d1e525ef95734caf34949d8b2d63b4fe5603226b5f632f2d7f927361ba639dc0e3c63414f45462342695916d5792133b4a24c7c4cbe2b97c712bf27ab62d3d68b3875d58ffe4b7c30a8171bff1a9e2f3995768faacda2ea9213ff35798b9e4513f6a87bd3f5a9d93e847e768359" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102432_0)
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
        
            key_len = unhexify( key_str, "84dd53ce0146cb71c32776033bb243098d78a22ac17f52a62a122f5653fb4e33" );
            pt_len = unhexify( src_str, "68222bffa782dcfe4f328fc20eb520e75a9a5fedbe13ec7fcf0e82fba08bb87a8a8e02902638e32fe0e2294344b380797f8028426ffcc0531c739c884892394c48ff0779c5f5edf0a36a3fb8aa91213347774ec4bf0fe1049bd53746b13beef3c637169826c367056cb1aa0a3868e23f886a9c7b8015c26af9e40794662f6b21" );
            iv_len = unhexify( iv_str, "f0c90a1bca52f30fab3670df0d3beab0" );
            add_len = unhexify( add_str, "a3ea8032f36a5ca3d7a1088fd08ac50ae6bdc06ad3a534b773ac3e3d4a3d524499e56274a0062c58c3b0685cc850f4725e5c221af8f51c6df2bbd5fbcff4a93ba4c1054f7f9c67fd9285511a08d328d76a642f067227d378f95a1e67587b90251f9103ed3cacdb6bf69e0794e366d8b92d8de37b4e028de0778841f356ac044d" );
            unhexify( tag_str, "b1ece9fb" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102432_1)
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
        
            key_len = unhexify( key_str, "9bb36fe25e966a075ae2c3bb43b5877679ebc379d5123c8eda3fa0e30b95cae0" );
            pt_len = unhexify( src_str, "fb3a4be643c10343251c6f0745aaa54349463f622ca04a792e9b4780866844b30aeef3269fc60cac0ea031c5f3780b535e15154f7c76eb4a371b8ae368550f3fa2ce693c34511ec96b839cac567f1b0de0e7e3116d729b45d1b16e453703a43db73f5d0c3e430f16b142420b5f0d26d72ac3dba543d7d813603b0bfdca3dd63e" );
            iv_len = unhexify( iv_str, "59869df4ef5754b406478a2fb608ee99" );
            add_len = unhexify( add_str, "ecd125682e8a8e26757c888b0c8b95dec5e7ed7ac991768f93e8af5bcf6f21ed4d4d38699ee7984ed13635fff72f938150157c9a27fcda121ffced7b492d2b18dad299cb6495ed5f68441aefc8219d2cf717d15d5cd2dbce4606fcf90fe45f3601127cf6acee210bd7df97309f773974a35bef1d33df984101c2fc9d4b55259e" );
            unhexify( tag_str, "cb3f5338" );
        
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


        FCT_TEST_BGN(gcm_nist_validation_aes_2561281024102432_2)
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
        
            key_len = unhexify( key_str, "ca264e7caecad56ee31c8bf8dde9592f753a6299e76c60ac1e93cff3b3de8ce9" );
            pt_len = unhexify( src_str, "8d03cf6fac31182ad3e6f32e4c823e3b421aef786d5651afafbf70ef14c00524ab814bc421b1d4181b4d3d82d6ae4e8032e43a6c4e0691184425b37320798f865c88b9b306466311d79e3e42076837474c37c9f6336ed777f05f70b0c7d72bd4348a4cd754d0f0c3e4587f9a18313ea2d2bace502a24ea417d3041b709a0471f" );
            iv_len = unhexify( iv_str, "4763a4e37b806a5f4510f69fd8c63571" );
            add_len = unhexify( add_str, "07daeba37a66ebe15f3d6451d1176f3a7107a302da6966680c425377e621fd71610d1fc9c95122da5bf85f83b24c4b783b1dcd6b508d41e22c09b5c43693d072869601fc7e3f5a51dbd3bc6508e8d095b9130fb6a7f2a043f3a432e7ce68b7de06c1379e6bab5a1a48823b76762051b4e707ddc3201eb36456e3862425cb011a" );
            unhexify( tag_str, "3105dddb" );
        
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

    }
    FCT_SUITE_END();

#endif /* POLARSSL_GCM_C */

}
FCT_END();

