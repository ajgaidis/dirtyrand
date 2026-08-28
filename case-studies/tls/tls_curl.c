#include <stdio.h>
#include <stdlib.h>
#include <curl/curl.h>
#include "libprimitive.h"

static void do_curl_request(void)
{
  CURL *curl;
  CURLcode res;

  /* Initialize libcurl */
  curl_global_init(CURL_GLOBAL_DEFAULT);

  /* Get a curl easy handle; this handle is used for a single transfer */
  curl = curl_easy_init();
  if (curl) {
    /*
     * Set the options for the transfer. This is where we
     * replicate the command-line flags.
     */

    /* Set the URL: https://static-rsa.badssl.com */
    curl_easy_setopt(curl, CURLOPT_URL, "https://static-rsa.badssl.com");

    /* Enable verbose output, equivalent to -v */
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    /* Disable SSL certificate verification, equivalent to --insecure or -k */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    /* Force the use of TLS 1.2, equivalent to --tlsv1.2 */
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);

    /* Set the desired cipher list, equivalent to --ciphers */
    curl_easy_setopt(curl, CURLOPT_SSL_CIPHER_LIST, "AES128-GCM-SHA256");

    /*
     * Perform the request. This call is blocking and will
     * perform the entire transfer
     */
    res = curl_easy_perform(curl);

    /* Check for errors */
    if (res != CURLE_OK) {
      fprintf(stderr, "curl_easy_perform() failed: %s\n",
              curl_easy_strerror(res));
      exit(EXIT_FAILURE);
    }

    /* Clean up the easy handle */
    curl_easy_cleanup(curl);
  }

  /* Clean up libcurl global state */
  curl_global_cleanup();
}

int main(void)
{
  /* Zap RNG  */
  trigger_overwrite_of_l1_key();

  /* Make curl request */
  do_curl_request();

  /* Success! */
  return EXIT_SUCCESS;
}
