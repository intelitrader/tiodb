#include <dlfcn.h>
#include <stdio.h>

int main()
{
    void* tioclient = dlopen("tioclient.so", RTLD_NOW);

    if( tioclient )
    {
        void (*test_function)() = (void(*)()) dlsym(tioclient, "test_function");

        if( test_function )
        {
            test_function();
        }
        else printf("Error loading test_function symbol\n");

        dlclose(tioclient);
    }
    else printf("Error loading tioclient.so\n");

    return 0;
}

