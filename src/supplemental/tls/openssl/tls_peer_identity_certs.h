// This software is supplied under the MIT License (LICENSE.txt).
//
// Test-only CA and server certificate with DNS and IP SANs.
// The private key is intentionally public test data.

static const char peer_ca_cert[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBrjCCAVOgAwIBAgIUXSrYUQ4Gjf7obQFXYig2JN52LYwwCgYIKoZIzj0EAwIw\n"
    "JDEiMCAGA1UEAwwZTk5HIHBlZXIgaWRlbnRpdHkgdGVzdCBDQTAeFw0yNjEwMDkw\n"
    "NjQ5MDNaFw0zNjEwMDYwNjQ5MDNaMCQxIjAgBgNVBAMMGU5ORyBwZWVyIGlkZW50\n"
    "aXR5IHRlc3QgQ0EwWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAAQbALXvL13VNeyi\n"
    "2BUyyX3YsncHm6Nl/7SyuRjLmaFI73ureOCDaxKmcs3H97+s052+SYuSw4mKcZGs\n"
    "zNv3N+hEo2MwYTAdBgNVHQ4EFgQUeDgDtBuBLH2M3J4JjR8Ns5lFln0wHwYDVR0j\n"
    "BBgwFoAUeDgDtBuBLH2M3J4JjR8Ns5lFln0wDwYDVR0TAQH/BAUwAwEB/zAOBgNV\n"
    "HQ8BAf8EBAMCAQYwCgYIKoZIzj0EAwIDSQAwRgIhAO9kVmXgUUN8qrkZNnwIrdVF\n"
    "X4ZOGWW4UixhTasMWGl2AiEA3LwlhGXXzwnjlej+WgXxB9NDfeaQ9g7BNlMs9G5w\n"
    "JFc=\n"
    "-----END CERTIFICATE-----\n"
    ;

static const char peer_server_cert[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBrzCCAVSgAwIBAgIUHPc5i0KoYTpxJFEYuOSIP4p7vuwwCgYIKoZIzj0EAwIw\n"
    "JDEiMCAGA1UEAwwZTk5HIHBlZXIgaWRlbnRpdHkgdGVzdCBDQTAeFw0yNjEwMDkw\n"
    "NjQ5MDNaFw0zNjEwMDYwNjQ5MDNaMBYxFDASBgNVBAMMC3NlcnZlci50ZXN0MFkw\n"
    "EwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEXxwFjwOoVQE+wUZIB8BD+lC9BG2dKCPm\n"
    "HabU4jFxpRU6y6A12GbURjY2z+61cD+Yv5fR0R/DLlUB7YPxJ7olbqNyMHAwLgYD\n"
    "VR0RBCcwJYILc2VydmVyLnRlc3SHBH8AAAGHEAAAAAAAAAAAAAAAAAAAAAEwHQYD\n"
    "VR0OBBYEFGvrlFsrPlNKrUJY+endXtO1XMJSMB8GA1UdIwQYMBaAFHg4A7QbgSx9\n"
    "jNyeCY0fDbOZRZZ9MAoGCCqGSM49BAMCA0kAMEYCIQD2wIHUpJWDvFe9WXgRtb/i\n"
    "7bShAgPg2yUzcc0GqiGyDAIhALXG2oxAQpjhqRk90yCkpUAFGeNKpsCXEi9sUzei\n"
    "ds9G\n"
    "-----END CERTIFICATE-----\n"
    ;

static const char peer_server_key[] =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgYTrpwOJBTby5+vxY\n"
    "kLbH/ZZJevffohkHTReRzp1i3FihRANCAARfHAWPA6hVAT7BRkgHwEP6UL0EbZ0o\n"
    "I+YdptTiMXGlFTrLoDXYZtRGNjbP7rVwP5i/l9HRH8MuVQHtg/EnuiVu\n"
    "-----END PRIVATE KEY-----\n"
    ;
