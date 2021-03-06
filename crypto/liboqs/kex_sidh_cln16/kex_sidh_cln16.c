#if defined(WINDOWS)
#pragma warning(disable : 4047 4090)
#endif

#if defined(WINDOWS)
#define UNUSED
#else
#define UNUSED __attribute__((unused))
#endif

#include <stdlib.h>
#include <string.h>
#if !defined(WINDOWS)
#include <strings.h>
#include <unistd.h>
#endif

#include "../kex/kex.h"
#include "../crypto/rand/rand.h"

#include "SIDH.h"
#include "kex_sidh_cln16.h"

#if defined(WINDOWS)
#define strdup _strdup // for strdup deprecation warning
#endif

static const char *P751 = "p751";
static const char *CompressedP751 = "compressedp751";

static int isCompressed(const char *named_parameters) {
	if (named_parameters != NULL && strcmp(named_parameters, CompressedP751) == 0) {
		return 1;
	}

	return 0;
}

// Check if curve isogeny structure is NULL
extern bool oqs_sidh_cln16_is_CurveIsogenyStruct_null(PCurveIsogenyStruct pCurveIsogeny);

OQS_KEX *OQS_KEX_sidh_cln16_new(OQS_RAND *rand, const char *named_parameters) {
	int compressed = isCompressed(named_parameters);
	OQS_KEX *k = malloc(sizeof(OQS_KEX));
	if (k == NULL) {
		return NULL;
	}

	// Curve isogeny system initialization
	PCurveIsogenyStruct curveIsogeny = oqs_sidh_cln16_curve_allocate(&CurveIsogeny_SIDHp751);

	if (curveIsogeny == NULL || oqs_sidh_cln16_is_CurveIsogenyStruct_null(curveIsogeny)) {
		free(k);
		oqs_sidh_cln16_curve_free(curveIsogeny);
		return NULL;
	}
	if (oqs_sidh_cln16_curve_initialize(curveIsogeny, &CurveIsogeny_SIDHp751) != SIDH_CRYPTO_SUCCESS) {
		free(k);
		oqs_sidh_cln16_curve_free(curveIsogeny);
		return NULL;
	}
	k->ctx = curveIsogeny;
	k->method_name = compressed ? strdup("SIDH CLN16 compressed") : strdup("SIDH CLN16");
	k->estimated_classical_security = 192;
	k->estimated_quantum_security = 128;
	k->seed = NULL;
	k->seed_len = 0;
	k->named_parameters = compressed ? (char*)CompressedP751 : (char*)P751;
	k->rand = rand;
	k->params = NULL;
	k->alice_0 = &OQS_KEX_sidh_cln16_alice_0;
	k->bob = &OQS_KEX_sidh_cln16_bob;
	k->alice_1 = &OQS_KEX_sidh_cln16_alice_1;
	k->alice_priv_free = &OQS_KEX_sidh_cln16_alice_priv_free;
	k->free = &OQS_KEX_sidh_cln16_free;

	return k;
}

int OQS_KEX_sidh_cln16_alice_0(OQS_KEX *k, void **alice_priv, uint8_t **alice_msg, size_t *alice_msg_len) {

	int ret;
	// non-compressed public key
	uint8_t *alice_tmp_pub = NULL;

	if (!k || !alice_priv || !alice_msg || !alice_msg_len) {
		return 0;
	}

	int compressed = isCompressed(k->named_parameters);
	*alice_priv = NULL;
	/* alice_msg is alice's public key */
	*alice_msg = NULL;
	if (compressed) {
		alice_tmp_pub = malloc(SIDH_PUBKEY_LEN);
		*alice_msg = malloc(SIDH_COMPRESSED_PUBKEY_LEN);
		if (alice_tmp_pub == NULL || *alice_msg == NULL) {
			goto err;
		}
	} else {
		// non-compressed
		*alice_msg = malloc(SIDH_PUBKEY_LEN);
		if (*alice_msg == NULL) {
			goto err;
		}
		alice_tmp_pub = *alice_msg; // point to the pub key
	}
	*alice_priv = malloc(SIDH_SECRETKEY_LEN);
	if (*alice_priv == NULL) {
		goto err;
	}

	// generate Alice's key pair
	if (oqs_sidh_cln16_EphemeralKeyGeneration_A((unsigned char *) *alice_priv, (unsigned char *) alice_tmp_pub, k->ctx, k->rand) != SIDH_CRYPTO_SUCCESS) {
		goto err;
	}

	if (compressed) {
		// compress Alice's public key
		oqs_sidh_cln16_PublicKeyCompression_A(alice_tmp_pub, (unsigned char *) *alice_msg, k->ctx);
		*alice_msg_len = SIDH_COMPRESSED_PUBKEY_LEN;
	} else {
		*alice_msg_len = SIDH_PUBKEY_LEN;
		alice_tmp_pub = NULL; // we don't want to double-free it
	}

	ret = 1;
	goto cleanup;

err:
	ret = 0;
	free(*alice_msg);
	*alice_msg = NULL;
	free(*alice_priv);
	*alice_priv = NULL;

cleanup:
	free(alice_tmp_pub);

	return ret;
}

int OQS_KEX_sidh_cln16_bob(OQS_KEX *k, const uint8_t *alice_msg, const size_t alice_msg_len, uint8_t **bob_msg, size_t *bob_msg_len, uint8_t **key, size_t *key_len) {

	int ret;
	uint8_t *bob_priv = NULL;
	// non-compressed public key
	uint8_t *bob_tmp_pub = NULL;
	// decompression values
	unsigned char *R = NULL, *A = NULL;

	if (!k || !alice_msg || !bob_msg || !bob_msg_len || !key || !key_len) {
		return 0;
	}

	*bob_msg = NULL;
	*key = NULL;

	int compressed = isCompressed(k->named_parameters);

	if (compressed) {
		if (alice_msg_len != SIDH_COMPRESSED_PUBKEY_LEN) {
			goto err;
		}
		bob_tmp_pub = malloc(SIDH_PUBKEY_LEN);
		*bob_msg = malloc(SIDH_COMPRESSED_PUBKEY_LEN);
		if (bob_tmp_pub == NULL || *bob_msg == NULL) {
			goto err;
		}
		A = malloc(SIDH_COMPRESSED_A_LEN);
		if (A == NULL) {
			goto err;
		}
		R = malloc(SIDH_COMPRESSED_R_LEN);
		if (R == NULL) {
			goto err;
		}
	} else {
		if (alice_msg_len != SIDH_PUBKEY_LEN) {
			goto err;
		}
		// non-compressed
		*bob_msg = malloc(SIDH_PUBKEY_LEN);
		if (*bob_msg == NULL) {
			goto err;
		}
		bob_tmp_pub = *bob_msg; // point to the pub key
	}

	bob_priv = malloc(SIDH_SECRETKEY_LEN);
	if (bob_priv == NULL) {
		goto err;
	}
	*key = malloc(SIDH_SHAREDKEY_LEN);
	if (*key == NULL) {
		goto err;
	}

	// generate Bob's key pair
	if (oqs_sidh_cln16_EphemeralKeyGeneration_B((unsigned char *) bob_priv, (unsigned char *) bob_tmp_pub, k->ctx, k->rand) != SIDH_CRYPTO_SUCCESS) {
		goto err;
	}

	if (compressed) {
		// compress Bob's public key
		oqs_sidh_cln16_PublicKeyCompression_B(bob_tmp_pub, (unsigned char *) *bob_msg, k->ctx);
		*bob_msg_len = SIDH_COMPRESSED_PUBKEY_LEN;
		// decompress Alice's public key
		oqs_sidh_cln16_PublicKeyADecompression_B((unsigned char *) bob_priv, (unsigned char *) alice_msg, R, A, k->ctx);
		// compute Bob's shared secret
		if (oqs_sidh_cln16_EphemeralSecretAgreement_Compression_B((unsigned char *) bob_priv, R, A, (unsigned char *) *key, k->ctx) != SIDH_CRYPTO_SUCCESS) {
			goto err;
		}
	} else {
		*bob_msg_len = SIDH_PUBKEY_LEN;
		bob_tmp_pub = NULL; // we don't want to double-free it
		// compute Bob's shared secret
		if (oqs_sidh_cln16_EphemeralSecretAgreement_B((unsigned char *) bob_priv, (unsigned char *) alice_msg, (unsigned char *) *key, k->ctx) != SIDH_CRYPTO_SUCCESS) {
			goto err;
		}
	}

	*key_len = SIDH_SHAREDKEY_LEN;

	ret = 1;
	goto cleanup;

err:
	ret = 0;
	free(*bob_msg);
	*bob_msg = NULL;
	free(*key);
	*key = NULL;

cleanup:
	free(bob_tmp_pub);
	free(bob_priv);
	free(A);
	free(R);

	return ret;
}

int OQS_KEX_sidh_cln16_alice_1(OQS_KEX *k, const void *alice_priv, const uint8_t *bob_msg, const size_t bob_msg_len, uint8_t **key, size_t *key_len) {

	int ret;
	// decompression values
	unsigned char *R = NULL, *A = NULL;

	if (!k || !alice_priv || !bob_msg || !key || !key_len) {
		return 0;
	}

	*key = NULL;

	int compressed = isCompressed(k->named_parameters);

	*key = malloc(SIDH_SHAREDKEY_LEN);
	if (*key == NULL) {
		goto err;
	}
	*key_len = SIDH_SHAREDKEY_LEN;

	if (compressed) {
		if (bob_msg_len != SIDH_COMPRESSED_PUBKEY_LEN) {
			goto err;
		}
		A = malloc(SIDH_COMPRESSED_A_LEN);
		if (A == NULL) {
			goto err;
		}
		R = malloc(SIDH_COMPRESSED_R_LEN);
		if (R == NULL) {
			goto err;
		}
		// compute Alice's shared secret
		oqs_sidh_cln16_PublicKeyBDecompression_A((unsigned char *) alice_priv, (unsigned char *) bob_msg, R, A, k->ctx);
		if (oqs_sidh_cln16_EphemeralSecretAgreement_Compression_A((unsigned char *) alice_priv, R, A, (unsigned char *) *key, k->ctx) != SIDH_CRYPTO_SUCCESS) {
			goto err;
		}
	} else {
		if (bob_msg_len != SIDH_PUBKEY_LEN) {
			goto err;
		}
		if (oqs_sidh_cln16_EphemeralSecretAgreement_A((unsigned char *) alice_priv, (unsigned char *) bob_msg, (unsigned char *) *key, k->ctx) != SIDH_CRYPTO_SUCCESS) {
			goto err;
		}
	}

	ret = 1;
	goto cleanup;

err:
	ret = 0;
	free(*key);
	*key = NULL;

cleanup:
	free(A);
	free(R);

	return ret;
}

void OQS_KEX_sidh_cln16_alice_priv_free(UNUSED OQS_KEX *k, void *alice_priv) {
	if (alice_priv) {
		free(alice_priv);
	}
}

void OQS_KEX_sidh_cln16_free(OQS_KEX *k) {
	if (!k) {
		return;
	}
	oqs_sidh_cln16_curve_free((PCurveIsogenyStruct) k->ctx);
	k->ctx = NULL;
	free(k->method_name);
	k->method_name = NULL;
	free(k);
}
