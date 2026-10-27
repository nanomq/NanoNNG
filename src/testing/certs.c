//
// Copyright 2020 Staysail Systems, Inc. <info@staysail.tech>
// Copyright 2018 Capitar IT Group BV <info@capitar.com>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

// TLS certificates.  These are pre-generated, and should not be used outside
// of these test cases.  They are all using RSA 2048 with SHA256.
// All certs are signed by the root key (making the root self-signed).
// They all expire in about 100 years -- so we don't have to worry about
// expiration.
//
// The server cert uses CN 127.0.0.1.
//
// Country = XX
// State = Utopia
// Locality = Paradise
// Organization = NNG Tests, Inc.
//

const char *nuts_server_key =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQCew9RLHKh4nAZv\n"
    "fkwmyjD7Rg5W8faPDE8/pBsbSgP9NFfHxynNudc/PL/1LWs96joJohcw4IQwokeP\n"
    "ek6+nmnlFW8Byp/5I7rpJFYYEMVqg2nROptznI+s18BIci5BTb6Pp2PwIsbS3Y9F\n"
    "cSlEiolooyDT17d4RrQHGyPRDlHkOHw6rqRVxB7yX6bGg6OvpuG8c2fsmc7u5OHr\n"
    "J0YMjTzi5iFWvPfo4q06v1z7C9bCuu5AaYdfI4uPEU2lP13xPls9qHuDqRyDNJwK\n"
    "Wm0cKUlfAmlAAzhZPYhZahtVA2cb3GE131a/VnWbEcCWpDApDj5gWe96NoGU56P4\n"
    "HVji7m2nAgMBAAECggEAAhvAU6heGw6aDfGpGebGKcAaFqYrS7uycw+UheiWDvnF\n"
    "T1OMxuejPSVrDdrRjsaaiLWMsKB6q5lWxp1YMwiFiCj1tW9hxSbn/N/3qLrS9FbU\n"
    "bpaO/ppjVLyfbWw0GGKWQkV1z/hA7bH1jMreQm5t5A7ZM8Gsn73zTBjcgT07HIMX\n"
    "fvovZi9nGvMXollHW5wsjHoNN6kAJG1NLW9PeqlJxO0hHraihhIs61N8EHEkaEIN\n"
    "medQvWV5xLkBSWwJSrjZ2DWzrioLk7nyefL/EE/NZc7OluQYLjEx1Jv+03J4lO4r\n"
    "5bCe6jsULLA/HuiL0+H652EnJeeeSNiFqpZRIx6R8QKBgQDTMlofdxnSUk6j7NRH\n"
    "g7IWpXifxb1Vz0lDqNKCTyaoAzLIKkcuwZTtGUUnHiLyBaWCi9kQ8bu35eMUt7KP\n"
    "rewliuvYuIIpfyiFF0dQHRcq1bgDBQtHxm3epVphQr/L8hrvOUd4hlb3fAblkqd1\n"
    "Rm0ZImLit3z0xfQKdFQPqpRuSQKBgQDAcgUvwR0Gz3aDjdKMDRx0ngJr4GZJce9F\n"
    "NlLtUelyARD4J1Xy6O+6AJxpdQR9LnyCXepg2ARb4zFbaa4gsXvPnRqY1Qmcgfsq\n"
    "fcUQdC2PToAcMiOlxw7MXxsrFktw+6Xx4WFGMiL24p6rBnA1amH31rSEXFWuysrb\n"
    "kO3wQu28bwKBgQDA4TJ3nCFD+RGDGk7AxWDG1/ZzDHefP8FsfmlKwxjNxqYBMsNx\n"
    "yy/E3hhO4nIN0ORYdnoWxH6pM9WQ3TfBipiprNc6RT/ywAP0kM77NqYq46a7c0bb\n"
    "FTjQZvlE7F4X15OLuScxxpb0TRxLXAV90ITguIvQ/LytG4CGb7k52ZLXqQKBgQCx\n"
    "1KsWRc0czfkl95fn8vWojZqPNP1QQQhpXJmk01x1DKcxqKezn6xmpMnkaU2Mn7hO\n"
    "f5plgzgD8R1a630MmPVgFDzPnY0UIsiFq1K+sZyoIFIhu/NU7WyvTfM9DY0JCoF9\n"
    "0lahFxNYMHGOeJSXx4ZgDvcgmHJU4vAxlOtKaY5l4QKBgES1nhhqmCmRl0cwYNBw\n"
    "JOBrGkOxTfDBkhMl8J86fS0lZrShj9sBkIwh+rXtTUbvqJWlmsvoj6e4BLxyyc76\n"
    "OnyZR2ETrBrzCVLJxPEhf+pRtWYpBD37aUUcpeOBaWsiK8Tw82CSIH6+AksHJpBs\n"
    "u3OcSo7p6J6/cALtoPaxqeI3\n"
    "-----END PRIVATE KEY-----\n";

const char *nuts_server_crt =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDkTCCAnmgAwIBAgIUauijyPWnP/4mjeV/0qUd9R5qMiIwDQYJKoZIhvcNAQEL\n"
    "BQAwTDELMAkGA1UEBhMCWFgxDzANBgNVBAgMBlV0b3BpYTEYMBYGA1UECgwPTk5H\n"
    "IFRlc3RzLCBJbmMuMRIwEAYDVQQDDAkxMjcuMC4wLjEwIBcNMjUxMDE5MTc1MTQ5\n"
    "WhgPMjEyNTA5MjUxNzUxNDlaMEwxCzAJBgNVBAYTAlhYMQ8wDQYDVQQIDAZVdG9w\n"
    "aWExGDAWBgNVBAoMD05ORyBUZXN0cywgSW5jLjESMBAGA1UEAwwJMTI3LjAuMC4x\n"
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAnsPUSxyoeJwGb35MJsow\n"
    "+0YOVvH2jwxPP6QbG0oD/TRXx8cpzbnXPzy/9S1rPeo6CaIXMOCEMKJHj3pOvp5p\n"
    "5RVvAcqf+SO66SRWGBDFaoNp0Tqbc5yPrNfASHIuQU2+j6dj8CLG0t2PRXEpRIqJ\n"
    "aKMg09e3eEa0Bxsj0Q5R5Dh8Oq6kVcQe8l+mxoOjr6bhvHNn7JnO7uTh6ydGDI08\n"
    "4uYhVrz36OKtOr9c+wvWwrruQGmHXyOLjxFNpT9d8T5bPah7g6kcgzScClptHClJ\n"
    "XwJpQAM4WT2IWWobVQNnG9xhNd9Wv1Z1mxHAlqQwKQ4+YFnvejaBlOej+B1Y4u5t\n"
    "pwIDAQABo2kwZzAdBgNVHQ4EFgQUe/pJlPE9yjI6EXl0qSyy+gFD4LYwHwYDVR0j\n"
    "BBgwFoAUe/pJlPE9yjI6EXl0qSyy+gFD4LYwDwYDVR0TAQH/BAUwAwEB/zAUBgNV\n"
    "HREEDTALgglsb2NhbGhvc3QwDQYJKoZIhvcNAQELBQADggEBAA64K0hKC1Tnn9BD\n"
    "sodvDqdo+C6MqgMiEicyQa+WNNs0dKZTyMYv8jGgWAGwXmDnOPjjx5whNkdksGso\n"
    "0jjkuqq+XmAdLTRi/a3LgT7Ta7xCcQ7epfIGcbIUzTDTX6fk5QCVlNhNjEV1zoS5\n"
    "7fpPrvRfEw28q6Ln/hVmJ5gnke2AUtJYAfY9KThFSWjjqLms2JmXujw2XAQsbzGy\n"
    "IrVUiXuBG1KreS9PU5M3IrLWK9AeHO66Cm2854mPDbSrzM7PcMpP+ME8A8PUoYbY\n"
    "pyiLWaLNCqXcskCrLnSyARL/kaWe4lAvkyZJ9wMVXdaSOoPOgHGJuSMAFn/bW5ls\n"
    "8zfq1fA=\n"
    "-----END CERTIFICATE-----\n";

const char *nuts_client_key =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQCZaUQUEbTjs/GO\n"
    "OAzfC3n7ZYo/+XMkmOI/6tOOpyEBytORggbyBMuxzAg52AqeViCiGwAe7UQjQuTN\n"
    "Av4uI8dc5y52+8kwHQAdoZScoxgKqzotJDhnal+/3U2X3TT8q0yHSmp60un29t4e\n"
    "3oHoT6gwRYLdfRYO3A+79Y0mZvTvwcpBfdhBN+CI2//V4LCfSjDrv0P1cP7pGqbF\n"
    "g5GgUp/CKMXznvRUl2ZS5S6T24EGLTckoGUe/FKEtKLYGVW77yVRSa/pSQ46Iis4\n"
    "vwnM3/Y61LuxjCZjVnesWdCdeVpfhhIKDKgLnGI4p9g154OzeM/8QNvkwqS+vqd7\n"
    "lXAHf3WzAgMBAAECggEAAZzgDjnjbadCaWIFKDT+hmMNJn3Rz4jf28Olu7C6fVQk\n"
    "+h6vkY7sfIdRkuOe+5Pg4DClejaVsfyVjA73qzicwuOZkxcq7hK2351Jrz77fPIw\n"
    "4m7ZEL9Ua6PvQR6D3JdCkhDr/P8FTMLUkxMo+ZpW2Zsz9GVsEDyVgY4htAtA4A6m\n"
    "0FmUARtkPNheQDPPdJ2nv5dLL/MlYWqxS8m3iBSvnk19Vw3Zbyt5/DY9UwoKjX7A\n"
    "6/JYssO0cSdcdQHD72zx+cJEsaaF8VA1dSIyxNl9x0pduVv98z3r3oLoGi2nIynA\n"
    "X8FBQSNoCpFMB5zCpvK33lxq9Xlk70ayx0FqmadPsQKBgQDYyEsGuXz1oVCZFhDr\n"
    "8VK5ouQLBD+qzpgUDeLUxqlTrOCWpdpFTO9bu5XmIsH8liOXKOM0GN7Xa3pT3xLM\n"
    "iKqjoL99jQx5ct0xlL+1qx3Ilmx094DiVb7GMaKvZHGcw9hUsaEzLgGxu/MpkYqX\n"
    "mJmz4Z5LIJChBQAwGAvuA3ZfcQKBgQC1Khou1r66t+nH7M45Lc6WPoHmNGenCOKK\n"
    "+rjaktEsK+GrlN8OX5OtDCpyR6gHT2Y2mmLcXnRwfBQs/4qTzVgDC6rv9s8kBMvh\n"
    "NulcAa6L/Bs6tuu3Iz+hVB4jcJYkoz9QbjEHL3/lVEC2UyVNE2LM9RwWo6/d+Wri\n"
    "DqB6A5TdYwKBgQDTUjzA62SC1aQJ91a7id3IlJ+Ulamyyf253ud138sEhIvSjw69\n"
    "a4gRdkVjuBI+NeWv3u3MxUnF2UHALL7Yn4koRPUmYK7+XRh+0rAnWg2Ikgpb40HC\n"
    "YS+9aHlcXJ/b35YsyZOInpqMNdmOdhASQ3HhxlhWiAI01Pkf6PEILlvgIQKBgCiH\n"
    "C6ZwyegxXm4oLpYiBjYcM6kRDiMS3MMkhZf4Ai00f96HhkLL5NhwgUphd1hzTbVn\n"
    "YjhUNQ4447aRUCnyZP8BbDMUbpBrNkTiqN+5TJfqRRkkdKTakNCBZeCdvijiEDbo\n"
    "/7TQzna6G2PuQ8jzTkX1i1wRMDWjJ2L8zK+e/31rAoGBAK/gTG/htoS3Qp0MCOAg\n"
    "Wc8dfzqyTCi+yHR6TJmEhgggJ1MdLtYlFXG4bufsvA5sn0CExx/2/mvstkQQLT+B\n"
    "CBOvqHRtb5/inFmycpJbuITi78KTp6uNKL0V7RsinN8coAcqs+NC4kU3S4cHM4Sv\n"
    "6TRe8fxDUTOp5FxX8yXs23je\n"
    "-----END PRIVATE KEY-----\n";

const char *nuts_client_crt =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDVDCCAjygAwIBAgIBATANBgkqhkiG9w0BAQsFADBMMQswCQYDVQQGEwJYWDEP\n"
    "MA0GA1UECAwGVXRvcGlhMRgwFgYDVQQKDA9OTkcgVGVzdHMsIEluYy4xEjAQBgNV\n"
    "BAMMCTEyNy4wLjAuMTAgFw0yNTEwMTkxNzUxNDlaGA8yMTI1MDkyNTE3NTE0OVow\n"
    "STELMAkGA1UEBhMCWFgxDzANBgNVBAgMBlV0b3BpYTEYMBYGA1UECgwPTk5HIFRl\n"
    "c3RzLCBJbmMuMQ8wDQYDVQQDDAZjbGllbnQwggEiMA0GCSqGSIb3DQEBAQUAA4IB\n"
    "DwAwggEKAoIBAQCZaUQUEbTjs/GOOAzfC3n7ZYo/+XMkmOI/6tOOpyEBytORggby\n"
    "BMuxzAg52AqeViCiGwAe7UQjQuTNAv4uI8dc5y52+8kwHQAdoZScoxgKqzotJDhn\n"
    "al+/3U2X3TT8q0yHSmp60un29t4e3oHoT6gwRYLdfRYO3A+79Y0mZvTvwcpBfdhB\n"
    "N+CI2//V4LCfSjDrv0P1cP7pGqbFg5GgUp/CKMXznvRUl2ZS5S6T24EGLTckoGUe\n"
    "/FKEtKLYGVW77yVRSa/pSQ46Iis4vwnM3/Y61LuxjCZjVnesWdCdeVpfhhIKDKgL\n"
    "nGI4p9g154OzeM/8QNvkwqS+vqd7lXAHf3WzAgMBAAGjQjBAMB0GA1UdDgQWBBQJ\n"
    "f7isP38nCilqwkILpUh39cTbnzAfBgNVHSMEGDAWgBR7+kmU8T3KMjoReXSpLLL6\n"
    "AUPgtjANBgkqhkiG9w0BAQsFAAOCAQEAFmaeZyvKrCWtnYw9DIta4Owt704Iyx3O\n"
    "ztMofubuDruJKiIm+MXu6hNth1HIwlsdf/rHsuOfnl92VwEGstsHRJgHgkeXkbs3\n"
    "BlYCmApp9Z43QNFB80ErZy6I3j8AYkT/rFBWej9Sk1Blu22x6fNNljaer0VEapMj\n"
    "BT7x2A/nOeim1bTZFY7VEr46oxVmaBP+Y1px9ISR4JamvUxKJe3Tp1oSkHmnzZ1i\n"
    "kjTonimEWy9x1GCRVmrCN1NI6F0CsrfeRdyFTvWI/5EBTmihFnPfy+zabdWBfYCd\n"
    "h6gPnIU1mUMEEVovNcHWsXtqNDU1UKlLeOBJ6loagteqR6rQJrp+CA==\n"
    "-----END CERTIFICATE-----\n";

const char *nuts_garbled_crt =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDdzCCAl8CFEzqJgxMn+OTdw7RjLtz8FlhrQ0HMA0GCSqGSIb3DQEBCwUAMHcx\n"
    "CzAJBgNVBAYTAlhYMQ8wDQYDVQQIDAZVdG9waWExETAPBgNVBAcMCFBhcmFkaXNl\n"
    "MRgwFgYDVQQKDA9OTkcgVGVzdHMsIEluYy4xFDASBgNVBAsMC0NsaWVudCBDZXJ0\n"
    "MRQwEgYDVQQDDAtUZXN0IENsaWVudDAgFw0yMDA1MjMxODQ1MjZaGA8yMTIwMDQy\n"
    "8884NDUyNlowdzELMAkGA1UEBhMCWFgxDzANBgNVBAgMBlV0b3BpYTERMA8GA1UE\n"
    "BwwIUGFyYWRpc2UxGDAWBgNVBAoMD05ORyBUZXN0cywgSW5jLjEUMBIGA1UECwwL\n"
    "Q2xpZW50IENlcnQxFDASBgNVBAMMC1Rlc3QgQ2xpZW50MIIBIjANBgkqhkiG9w0B\n"
    "AQEFAAOCAQ8AMIIBCgKCAQEAoHWEJXvfaHDM33AyYbJHggKOllgcvwscEnsXztIt\n"
    "OK+0jO6SRFSbtye1cjtrkGVCYBjeWMcOdEiNB0pw3PceVpF/Q9ifCuaSYsJA3sPH\n"
    "wi/A3G7ZTe2KCH1i26I4zyw1Bn5AzkaDDXsaht2S9PEqIBCbWo/V1pWiv4QdYmLT\n"
    "/UFYJDxFpFC3iKVC+BDv9yzziyaFXOYsQJXcaq8ZRD79bNV5NFfzUih8RoasIdD4\n"
    "LoamBSbbr5XzstTISus+wu1JDKgKkYMJhLGA/tdU/eOKuTDx89yO4ba23W74xeqW\n"
    "JYe0wPy+krmeB5M7UA7jIvg1JXhYACxujhieMp7wcC3FPwIDAQABMA0GCSqGSIb3\n"
    "DQEBCwUAA4IBAQCMTQ89YnD19bCGIdUl/z6w2yx1x1kvTYHT+SzhUprsgiuS3KT1\n"
    "RZNhjf5U3Yu+B6SrJCLuylv+L2zQfmHogp3lV7bayOA7r/rVy5fdmHS+Ei1w6LDL\n"
    "t8jayiRMPG4VCgaG486yI73PFpK5DXnyFqSd23TlWvNoNeVag5gjlhzG+mHZBSB2\n"
    "ExpGY3SPxrKSzDqIITVPVgzjW25N8qtgLXC6HODDiViNYq1nmuoS4O80NIYAPPs6\n"
    "sxUMa5kT+zc17q57ZcgNq/sSGI3BU4b/E/8ntIwiui2xWSf/4JR6xtanih8uY5Pu\n"
    "QTgg9qTtFgtu4WWUP7JhreoINTw6O4/g5Z18\n"
    "-----END CERTIFICATE-----\n";
