#include "srp.h"

static int library_initialized = 0;
static int srp_modulus_min_bits = SRP_DEFAULT_MIN_BITS;

_TYPE( SRP_RESULT )
SRP_initialize_library()
{
  if(library_initialized == 0) {
    BigIntegerInitialize();
    t_stronginitrand();
    library_initialized = 1;
  }
  return SRP_SUCCESS;
}

_TYPE( SRP_RESULT )
SRP_finalize_library()
{
  if(library_initialized > 0) {
    library_initialized = 0;
    BigIntegerFinalize();
  }
  return SRP_SUCCESS;
}

_TYPE( SRP * )
SRP_new(SRP_METHOD * meth)
{
  SRP * srp = (SRP *) malloc(sizeof(SRP));

  if(srp == NULL)
    return NULL;

  srp->flags = 0;
  srp->username = cstr_new();
  srp->bctx = BigIntegerCtxNew();
  srp->modulus = NULL;
  srp->accel = NULL;
  srp->generator = NULL;
  srp->salt = NULL;
  srp->verifier = NULL;
  srp->password = NULL;
  srp->pubkey = NULL;
  srp->secret = NULL;
  srp->u = NULL;
  srp->key = NULL;
  srp->ex_data = cstr_new();
  srp->param_cb = NULL;
  srp->meth = meth;
  srp->meth_data = NULL;
  if(srp->meth->init == NULL || (*srp->meth->init)(srp) == SRP_SUCCESS)
    return srp;
  free(srp);
  return NULL;
}

_TYPE( SRP_RESULT )
SRP_free(SRP * srp)
{
  if(srp->meth->finish)
    (*srp->meth->finish)(srp);

  if(srp->username)
    cstr_clear_free(srp->username);
  if(srp->modulus)
    BigIntegerFree(srp->modulus);
  if(srp->accel)
    BigIntegerModAccelFree(srp->accel);
  if(srp->generator)
    BigIntegerFree(srp->generator);
  if(srp->salt)
    cstr_clear_free(srp->salt);
  if(srp->verifier)
    BigIntegerClearFree(srp->verifier);
  if(srp->password)
    BigIntegerClearFree(srp->password);
  if(srp->pubkey)
    BigIntegerFree(srp->pubkey);
  if(srp->secret)
    BigIntegerClearFree(srp->secret);
  if(srp->u)
    BigIntegerFree(srp->u);
  if(srp->key)
    BigIntegerClearFree(srp->key);
  if(srp->bctx)
    BigIntegerCtxFree(srp->bctx);
  if(srp->ex_data)
    cstr_clear_free(srp->ex_data);
  free(srp);
  return SRP_SUCCESS;
}

_TYPE( SRP_RESULT )
SRP_set_username(SRP * srp, const char * username)
{
  cstr_set(srp->username, username);
  return SRP_SUCCESS;
}

_TYPE( SRP_RESULT )
SRP_set_params(SRP * srp, const unsigned char * modulus, int modlen,
	       const unsigned char * generator, int genlen,
	       const unsigned char * salt, int saltlen)
{
  SRP_RESULT rc;

  if(modulus == NULL || generator == NULL || salt == NULL)
    return SRP_ERROR;

  /* Set fields in SRP context */
  srp->modulus = BigIntegerFromBytes(modulus, modlen);
  if(srp->flags & SRP_FLAG_MOD_ACCEL)
    srp->accel = BigIntegerModAccelNew(srp->modulus, srp->bctx);
  srp->generator = BigIntegerFromBytes(generator, genlen);
  if(srp->salt == NULL)
    srp->salt = cstr_new();
  cstr_setn(srp->salt, (const char *)salt, saltlen);

  /* Now attempt to validate parameters */
  if(BigIntegerBitLen(srp->modulus) < SRP_get_modulus_min_bits())
    return SRP_ERROR;

  if(srp->param_cb) {
    rc = (*srp->param_cb)(srp, modulus, modlen, generator, genlen);
    if(!SRP_OK(rc))
      return rc;
  }

  return (*srp->meth->params)(srp, modulus, modlen, generator, genlen,
			      salt, saltlen);
}

_TYPE( SRP_RESULT )
SRP_set_auth_password(SRP * srp, const char * password)
{
  return (*srp->meth->passwd)(srp, (const unsigned char *)password,
			      strlen(password));
}

_TYPE( SRP_RESULT )
SRP_gen_pub(SRP * srp, cstr ** result)
{
  return (*srp->meth->genpub)(srp, result);
}

_TYPE( SRP_RESULT )
SRP_compute_key(SRP * srp, cstr ** result,
		const unsigned char * pubkey, int pubkeylen)
{
  return (*srp->meth->key)(srp, result, pubkey, pubkeylen);
}

_TYPE( SRP_RESULT )
SRP_verify(SRP * srp, const unsigned char * proof, int prooflen)
{
  return (*srp->meth->verify)(srp, proof, prooflen);
}

_TYPE( SRP_RESULT )
SRP_respond(SRP * srp, cstr ** proof)
{
  return (*srp->meth->respond)(srp, proof);
}

_TYPE( SRP_RESULT )
SRP_set_modulus_min_bits(int minbits)
{
  srp_modulus_min_bits = minbits;
  return SRP_SUCCESS;
}

_TYPE( int )
SRP_get_modulus_min_bits()
{
  return srp_modulus_min_bits;
}

static int
default_secret_bits_cb(int modsize)
{
  return 256;
  /*return modsize;*/   /* Warning: Very Slow */
}

static SRP_SECRET_BITS_CB srp_sb_cb = default_secret_bits_cb;

_TYPE( SRP_RESULT )
SRP_set_secret_bits_cb(SRP_SECRET_BITS_CB cb)
{
  srp_sb_cb = cb;
  return SRP_SUCCESS;
}

_TYPE( int )
SRP_get_secret_bits(int modsize)
{
  return (*srp_sb_cb)(modsize);
}

_TYPE( SRP_RESULT )
SRP_add_ex_data(SRP * srp, const unsigned char * data, int datalen)
{
  cstr_appendn(srp->ex_data, (const char *)data, datalen);
  return SRP_SUCCESS;
}
