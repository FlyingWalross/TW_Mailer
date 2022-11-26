#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ldap.h>
#include <string>

bool LDAPauthenticate(std::string username, std::string password){

    const char *ldapUri = "ldap://ldap.technikum-wien.at:389";
    const int ldapVersion = LDAP_VERSION3;
    LDAP *ldapHandle;

    char ldapBindUser[256];
    const char* ldapBindPassword;
    
    sprintf(ldapBindUser, "uid=%s,ou=people,dc=technikum-wien,dc=at", username.c_str());
    ldapBindPassword = password.c_str();
    

    if(ldap_initialize(&ldapHandle, ldapUri) != LDAP_SUCCESS){
        fprintf(stderr, "ldap_init failed\n");
        perror("ldap_initialize");
        exit(EXIT_FAILURE);
    }

    //printf("connected to LDAP server %s\n", ldapUri);

    if(ldap_set_option(ldapHandle, LDAP_OPT_PROTOCOL_VERSION, &ldapVersion) != LDAP_SUCCESS){
        fprintf(stderr, "ldap_set_option failed\n");
        perror("ldap_set_option");
        ldap_unbind_ext_s(ldapHandle, NULL, NULL);
        exit(EXIT_FAILURE);
    }

    int rc = 0; // return code

    if((rc = ldap_start_tls_s(ldapHandle, NULL, NULL)) != LDAP_SUCCESS){
        fprintf(stderr, "ldap_start_tls_s(): %s\n", ldap_err2string(rc));
        ldap_unbind_ext_s(ldapHandle, NULL, NULL);
        exit(EXIT_FAILURE);
    };

    BerValue bindCredentials;
    bindCredentials.bv_val = (char *)ldapBindPassword;
    bindCredentials.bv_len = strlen(ldapBindPassword);
    BerValue *servercredp; // server's credentials

    if((rc = ldap_sasl_bind_s(ldapHandle, ldapBindUser, LDAP_SASL_SIMPLE, &bindCredentials, NULL, NULL, &servercredp)) != LDAP_SUCCESS){
        //fprintf(stderr, "LDAP bind error: %s\n", ldap_err2string(rc));
        ldap_unbind_ext_s(ldapHandle, NULL, NULL);
        return false;
    };

    ldap_unbind_ext_s(ldapHandle, NULL, NULL);
    return true;
}