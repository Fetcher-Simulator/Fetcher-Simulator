#include <bcrypt.h>

#include <exception>
#include <iostream>
#include <string>

int main()
{
    try
    {
        constexpr char password[] = "openbsd-ci-password";
        const std::string hash = Bcrypt::hash(password, 4);

        if (!Bcrypt::verify(password, hash))
        {
            std::cerr << "bcrypt verification rejected the correct password\n";
            return 1;
        }

        if (Bcrypt::verify("wrong-password", hash))
        {
            std::cerr << "bcrypt verification accepted an incorrect password\n";
            return 1;
        }

        std::cout << "OpenBSD bcrypt smoke test passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "bcrypt smoke test failed: " << error.what() << '\n';
        return 1;
    }
}
