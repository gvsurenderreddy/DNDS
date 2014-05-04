/*
 * Dynamic Network Directory Service
 * Copyright (C) 2009-2014
 * Nicolas J. Bouliane <nib@dynvpn.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; version 3 of the license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details
 */

/*
	x509.h undef X509_NAME because wincrypt.h define it.
	use winsock2.h, because it includes wincrypt.h and other usefull stuff
*/

#ifdef _WIN32
#include <winsock2.h>
#endif

#include <pthread.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "crypto.h"
#include "logger.h"


static int s_server_session_id_context = 1;
static int s_server_auth_session_id_context = 2;

passport_t *pki_passport_load_from_memory(char *certificate, char *privatekey, char *trusted_authority)
{
	BIO *bio_memory = NULL;
	passport_t *passport;
	X509 *trusted_authority_certificate;

	// create an empty passport
	passport = calloc(1, sizeof(passport_t));

	// fetch the certificate in PEM format and convert to X509
	bio_memory = BIO_new_mem_buf(certificate, strlen(certificate));
	passport->certificate = PEM_read_bio_X509(bio_memory, NULL, NULL, NULL);
	BIO_free(bio_memory);

	// fetch the private key in PEM format and convert to EVP
	bio_memory = BIO_new_mem_buf(privatekey, strlen(privatekey));
	passport->keyring = PEM_read_bio_PrivateKey(bio_memory, NULL, NULL, NULL);
	BIO_free(bio_memory);

	// fetch the certificate authority in PEM format convert to X509
	// and add to the trusted store
	bio_memory = BIO_new_mem_buf(trusted_authority, strlen(trusted_authority));
	trusted_authority_certificate = PEM_read_bio_X509(bio_memory, NULL, NULL, NULL);
	passport->trusted_authority = X509_STORE_new();
	X509_STORE_add_cert(passport->trusted_authority, trusted_authority_certificate);

	return passport;
}

passport_t *pki_passport_load_from_file(const char *certificate_filename,
					const char *privatekey_filename,
					const char *trusted_authority_filename)
{
	BIO *bio_file = NULL;
	passport_t *passport = NULL;
	X509 *trusted_authority_certificate;

	if (!certificate_filename || !privatekey_filename || !trusted_authority_filename) {
		return NULL;
	}

	// create an empty passport
	passport = calloc(1, sizeof(passport_t));

	// fetch the certificate in PEM format and convert to X509
	bio_file = BIO_new_file(certificate_filename, "r");
	if (bio_file == NULL) {
		free(passport);
		return NULL;
	}
	passport->certificate = PEM_read_bio_X509(bio_file, NULL, NULL, NULL);
	BIO_free(bio_file);

	// fetch the private key in PEM format and convert to EVP
	bio_file = BIO_new_file(privatekey_filename, "r");
		if (bio_file == NULL) {
		free(passport);
		return NULL;
	}
	passport->keyring = PEM_read_bio_PrivateKey(bio_file, NULL, NULL, NULL);
	BIO_free(bio_file);

	// fetch the certificate authority in PEM format convert to X509
	// and add to the trusted store
	bio_file = BIO_new_file(trusted_authority_filename, "r");
	if (bio_file == NULL) {
		free(passport);
		return NULL;
	}
	trusted_authority_certificate = PEM_read_bio_X509(bio_file, NULL, NULL, NULL);
	passport->trusted_authority = X509_STORE_new();
	X509_STORE_add_cert(passport->trusted_authority, trusted_authority_certificate);
	BIO_free(bio_file);

	return passport;
}

static pthread_mutex_t *lock_cs;
static long *lock_count;

void pthreads_locking_callback(int mode, int type, char *file, int line)
{
	(void)(file); // unused
	(void)(line);

	if (mode & CRYPTO_LOCK) {
		pthread_mutex_lock(&(lock_cs[type]));
		lock_count[type]++;
	} else {
		pthread_mutex_unlock(&(lock_cs[type]));
	}
}

unsigned long pthreads_thread_id(void)
{
	unsigned long ret;
	ret = (unsigned long)pthread_self();
	return(ret);
}

void thread_setup(void)
{
	int i;

	lock_cs = OPENSSL_malloc(CRYPTO_num_locks() * sizeof(pthread_mutex_t));
	lock_count = OPENSSL_malloc(CRYPTO_num_locks() * sizeof(long));

	for (i=0; i < CRYPTO_num_locks(); i++) {
		lock_count[i] = 0;
		pthread_mutex_init(&(lock_cs[i]),NULL);
	}

	CRYPTO_set_id_callback((unsigned long (*)())pthreads_thread_id);
	CRYPTO_set_locking_callback((void (*)())pthreads_locking_callback);
}

static DH *get_dh_1024() {

	static unsigned char dh1024_p[]={
		0xDE,0xD3,0x80,0xD7,0xE1,0x8E,0x1B,0x5D,0x5C,0x76,0x61,0x79,
		0xCA,0x8E,0xCD,0xAD,0x83,0x49,0x9E,0x0B,0xC0,0x2E,0x67,0x33,
	        0x5F,0x58,0x30,0x9C,0x13,0xE2,0x56,0x54,0x1F,0x65,0x16,0x27,
	        0xD6,0xF0,0xFD,0x0C,0x62,0xC4,0x4F,0x5E,0xF8,0x76,0x93,0x02,
	        0xA3,0x4F,0xDC,0x2F,0x90,0x5D,0x77,0x7E,0xC6,0x22,0xD5,0x60,
	        0x48,0xF5,0xFB,0x5D,0x46,0x5D,0xF5,0x97,0x20,0x35,0xA6,0xEE,
	        0xC0,0xA0,0x89,0xEE,0xAB,0x22,0x68,0x96,0x8B,0x64,0x69,0xC7,
	        0xEB,0x41,0xDF,0x74,0xDF,0x80,0x76,0xCF,0x9B,0x50,0x2F,0x08,
	        0x13,0x16,0x0D,0x2E,0x94,0x0F,0xEE,0x29,0xAC,0x92,0x7F,0xA6,
	        0x62,0x49,0x41,0x0F,0x54,0x39,0xAD,0x91,0x9A,0x23,0x31,0x7B,
	        0xB3,0xC9,0x34,0x13,0xF8,0x36,0x77,0xF3,
	};

	static unsigned char dh1024_g[]={
		0x02,
	};

	DH *dh;

	dh = DH_new();
	if (dh == NULL) {
		return NULL;
	}

	dh->p = BN_bin2bn(dh1024_p, sizeof(dh1024_p), NULL);
	dh->g = BN_bin2bn(dh1024_g, sizeof(dh1024_g), NULL);

	if (dh->p == NULL || dh->g == NULL) {
		DH_free(dh);
		return NULL;
	}

	return dh;
}

// FIXME - could we remove this function?
static DH *tmp_dh_callback(SSL *s, int is_export, int keylength)
{
	(void)(s); /* unused */
	(void)(is_export); /* unused */

	jlog(L_NOTICE, "keyl %i\n", keylength);
	return NULL;
}

static void ssl_error_stack()
{
	const char *file;
	int line;
	unsigned long e;

	do {
		e = ERR_get_error_line(&file, &line);
		jlog(L_ERROR, "%s", ERR_error_string(e, NULL));
	} while (e);
}

static long post_handshake_check(krypt_t *krypt)
{
	X509 *cert;
	X509_NAME *subj_ptr;

	cert = SSL_get_peer_certificate(krypt->ssl);
	if (cert == NULL)
		return 0;

	subj_ptr = X509_get_subject_name(cert);
	X509_NAME_get_text_by_NID(subj_ptr, NID_commonName, krypt->client_cn, 256);

	jlog(L_NOTICE, "CN=%s", krypt->client_cn);

	return 0;
}

static int verify_callback(int ok, X509_STORE_CTX *store)
{
	(void)(store); /* unused */

	/* FIXME Verify Not Before / Not After.. etc */
	return ok;
}

static int krypt_set_adh(krypt_t *kconn)
{
	jlog(L_NOTICE, "set adh");

	SSL_CTX_set_cipher_list(kconn->ctx, "ADH");
	DH *dh = get_dh_1024();
	SSL_CTX_set_tmp_dh(kconn->ctx, dh);
	DH_free(dh);

	SSL_CTX_set_tmp_dh_callback(kconn->ctx, tmp_dh_callback);
	SSL_CTX_set_verify(kconn->ctx, SSL_VERIFY_NONE, NULL);

	return 0;
}

// XXX Clean up this function, we MUST handle all errors possible
int krypt_set_rsa(krypt_t *kconn)
{
	jlog(L_NOTICE, "set rsa");

	if (kconn->security_level == KRYPT_RSA) {
		jlog(L_NOTICE, "the security level is already set to RSA");
		return 0;
	}

	SSL_set_cipher_list(kconn->ssl, "AES256-SHA");

	// Load the trusted certificate store into our SSL_CTX
	SSL_CTX_set_cert_store(kconn->ctx, kconn->passport->trusted_authority);

	// Force the peer cert verifying + fail if no cert is sent by the peer
	SSL_set_verify(kconn->ssl, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, verify_callback);

	// Set the certificate and key
	SSL_use_certificate(kconn->ssl, kconn->passport->certificate);
	SSL_use_PrivateKey(kconn->ssl, kconn->passport->keyring);

	if (kconn->conn_type == KRYPT_SERVER) {
		jlog(L_NOTICE, "set verify");

			// Change the session id to avoid resuming ADH session
		SSL_set_session_id_context(kconn->ssl, (void*)&s_server_auth_session_id_context,
							sizeof(s_server_auth_session_id_context));
	}

	kconn->security_level = KRYPT_RSA;

	return 0;
}

void krypt_add_passport(krypt_t *kconn, passport_t *passport)
{
	kconn->passport = passport;
}

void krypt_set_renegotiate(krypt_t *kconn)
{
	if (kconn->conn_type == KRYPT_SERVER) {

		kconn->status = KRYPT_HANDSHAKE;
		// bring back the connection to handshake mode
		kconn->ssl->state = SSL_ST_ACCEPT;
	}
}

int krypt_do_handshake(krypt_t *kconn, uint8_t *buf, size_t buf_data_size)
{
	int ret = 0;
	int nbyte = 0;
	int status = -1;

	if (buf != NULL && buf_data_size > 0) {
		nbyte = BIO_write(kconn->network_bio, buf, buf_data_size);
	}

	ret = SSL_do_handshake(kconn->ssl);

	jlog(L_NOTICE, "SSL state: %s", SSL_state_string_long(kconn->ssl));

	if (ret > 0 && !SSL_is_init_finished(kconn->ssl)) {
		// Need more data to continue ?
		jlog(L_ERROR, "handshake need more data to continue ??");
		status = 1;
	}
	else if (ret > 0 && SSL_is_init_finished(kconn->ssl)) {
		// Handshake successfully completed
		post_handshake_check(kconn);
		kconn->status = KRYPT_SECURE;
		status = 0;

		jlog(L_NOTICE, "cipher: %s", SSL_get_cipher_name(kconn->ssl));
	}
	else if (ret == 0) {
		// Error
		kconn->status = KRYPT_FAIL;
		jlog(L_ERROR, "ssl_get_error: %d\n", SSL_get_error(kconn->ssl, ret));
		ssl_error_stack();
		jlog(L_ERROR, "handshake error");
		status = -1;
	}
	else if (ret < 0) {
		// Need more data to continue
		status = 1;
	}

	nbyte = BIO_ctrl_pending(kconn->network_bio);

	if (nbyte > 0) { // Read pending data into the BIO
		nbyte = BIO_read(kconn->network_bio, kconn->buf_encrypt, kconn->buf_encrypt_size);
		kconn->buf_encrypt_data_size = nbyte; // FIXME dynamic buffer
	}

	return status;
}

int krypt_push_encrypted_data(krypt_t *kconn, uint8_t *buf, size_t buf_data_size)
{
	int nbyte;
	nbyte = BIO_write(kconn->network_bio, buf, buf_data_size);

	return nbyte;
}

int krypt_decrypt_buf(krypt_t *kconn)
{
	int nbyte = 0;
	int error = 0;
	int status = -1;

	nbyte = SSL_read(kconn->ssl, kconn->buf_decrypt, kconn->buf_decrypt_size);

	if (nbyte <= 0) {
		// SSL_read() failed
		status = -1;
		kconn->buf_decrypt_data_size = 0;
		error = SSL_get_error(kconn->ssl, nbyte);

		switch (error) {
			case SSL_ERROR_WANT_READ:
				nbyte = BIO_read(kconn->network_bio, kconn->buf_encrypt, kconn->buf_encrypt_size);
				kconn->buf_encrypt_data_size = nbyte; // FIXME dynamic buffer
				break;

			case SSL_ERROR_WANT_WRITE:
				break;

			default:
				jlog(L_ERROR, "<%s> SSL error", __func__);
				ssl_error_stack();
				return -1;
		}
	}
	else {
		// SSL_read() successful
		status = 0;
		kconn->buf_decrypt_data_size = nbyte;
	}
	return status;
}

int krypt_encrypt_buf(krypt_t *kconn, uint8_t *buf, size_t buf_data_size)
{
	int nbyte = 0;
	int pbyte = 0;
	int error = 0;
	int status = 0;
	nbyte = SSL_write(kconn->ssl, buf, buf_data_size);

	if (nbyte <= 0) {
		error = SSL_get_error(kconn->ssl, nbyte);
		switch (error) {

		case SSL_ERROR_WANT_READ:
			break;
		case SSL_ERROR_WANT_WRITE:
			status = -1;
			break;
		default:
			ssl_error_stack();
			return -1;
		}
	}

	pbyte = BIO_ctrl_pending(kconn->network_bio);

	kconn->buf_encrypt_data_size = 0;
	nbyte = BIO_read(kconn->network_bio, kconn->buf_encrypt, pbyte);

	if (nbyte > 0) {
		kconn->buf_encrypt_data_size = nbyte;
	}

	return status;
}

int krypt_secure_connection(krypt_t *kconn, uint8_t protocol, uint8_t conn_type, uint8_t security_level)
{
	switch (protocol) {

		case KRYPT_TLS:
			kconn->ctx = SSL_CTX_new(TLSv1_method());
			break;

		default:
			jlog(L_ERROR, "unknown protocol");
			return -1;
	}

	if (kconn->ctx == NULL) {
		jlog(L_ERROR, "unable to create SSL context");
		ssl_error_stack();
		return -1;
	}

	SSL_CTX_set_session_id_context(kconn->ctx,
		(void*)&s_server_session_id_context,
		sizeof(s_server_session_id_context));

	if (security_level == KRYPT_ADH)
		krypt_set_adh(kconn);

	// Create the BIO pair
	BIO_new_bio_pair(&kconn->internal_bio, 0, &kconn->network_bio, 0);

	// Create the SSL object
	kconn->ssl = SSL_new(kconn->ctx);
	SSL_set_bio(kconn->ssl, kconn->internal_bio, kconn->internal_bio);
	SSL_set_mode(kconn->ssl, SSL_MODE_AUTO_RETRY);

	if (security_level == KRYPT_RSA)
		krypt_set_rsa(kconn);

	kconn->conn_type = conn_type;
	switch (conn_type) {

		case KRYPT_SERVER:
			jlog(L_NOTICE, "connection type server");
			SSL_set_accept_state(kconn->ssl);

			break;

		case KRYPT_CLIENT:
			jlog(L_NOTICE, "connection type client");
			SSL_set_connect_state(kconn->ssl);

			break;

		default:
			jlog(L_ERROR, "unknown connection type");
			return -1;
	}

	kconn->status = KRYPT_HANDSHAKE;

	return 0;
}

void krypt_fini()
{

}

int krypt_init()
{
	SSL_library_init();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();
	thread_setup();

	return 0;
}


