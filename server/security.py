import hashlib
import hmac
import secrets


def hash_password(password: str) -> str:
    salt = secrets.token_bytes(16)
    digest = hashlib.scrypt(password.encode(), salt=salt, n=16384, r=8, p=1)
    return f"scrypt${salt.hex()}${digest.hex()}"


def verify_password(password: str, encoded: str) -> bool:
    try:
        name, salt, digest = encoded.split("$")
        if name != "scrypt":
            return False
        actual = hashlib.scrypt(
            password.encode(), salt=bytes.fromhex(salt), n=16384, r=8, p=1
        )
        return hmac.compare_digest(actual, bytes.fromhex(digest))
    except (ValueError, TypeError):
        return False


def token_hash(token: str) -> str:
    return hashlib.sha256(token.encode()).hexdigest()
