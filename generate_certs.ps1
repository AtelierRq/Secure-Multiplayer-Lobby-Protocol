# ============================================
# SMLP Certificate Generator
# ============================================

$CERT_DIR = "certs"

Write-Host ""
Write-Host "=== SMLP Certificate Generator ==="
Write-Host ""

if (!(Test-Path $CERT_DIR))
{
    New-Item -ItemType Directory -Path $CERT_DIR | Out-Null
}

# --------------------------------------------
# CA
# --------------------------------------------

Write-Host "[1/6] Generating CA key..."

openssl genrsa `
-out "$CERT_DIR/ca_key.pem" `
2048

Write-Host "[2/6] Generating CA certificate..."

openssl req `
-x509 `
-new `
-nodes `
-key "$CERT_DIR/ca_key.pem" `
-sha256 `
-days 3650 `
-out "$CERT_DIR/ca_cert.pem" `
-subj "/C=PL/O=SMLP/CN=SMLP-CA"

# --------------------------------------------
# SERVER
# --------------------------------------------

Write-Host "[3/6] Generating server key..."

openssl genrsa `
-out "$CERT_DIR/server_key.pem" `
2048

Write-Host "[4/6] Generating server certificate..."

openssl req `
-new `
-key "$CERT_DIR/server_key.pem" `
-out "$CERT_DIR/server.csr" `
-subj "/C=PL/O=SMLP/CN=127.0.0.1"

openssl x509 `
-req `
-in "$CERT_DIR/server.csr" `
-CA "$CERT_DIR/ca_cert.pem" `
-CAkey "$CERT_DIR/ca_key.pem" `
-CAcreateserial `
-out "$CERT_DIR/server_cert.pem" `
-days 365 `
-sha256

# --------------------------------------------
# CLIENT
# --------------------------------------------

Write-Host "[5/6] Generating client key..."

openssl genrsa `
-out "$CERT_DIR/client_key.pem" `
2048

Write-Host "[6/6] Generating client certificate..."

openssl req `
-new `
-key "$CERT_DIR/client_key.pem" `
-out "$CERT_DIR/client.csr" `
-subj "/C=PL/O=SMLP/CN=SMLP-Client"

openssl x509 `
-req `
-in "$CERT_DIR/client.csr" `
-CA "$CERT_DIR/ca_cert.pem" `
-CAkey "$CERT_DIR/ca_key.pem" `
-CAcreateserial `
-out "$CERT_DIR/client_cert.pem" `
-days 365 `
-sha256

# --------------------------------------------
# CLEANUP
# --------------------------------------------

Remove-Item "$CERT_DIR/server.csr" -ErrorAction SilentlyContinue
Remove-Item "$CERT_DIR/client.csr" -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "==================================="
Write-Host "Certificates generated successfully"
Write-Host "==================================="
Write-Host ""

Get-ChildItem $CERT_DIR