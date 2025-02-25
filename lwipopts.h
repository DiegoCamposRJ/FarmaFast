#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#define LWIP_HTTPD 1           // Habilita o servidor HTTP
#define LWIP_HTTPD_SSI 0       // Desabilita SSI (não necessário neste caso)
#define LWIP_HTTPD_CGI 0       // Desabilita CGI (não necessário neste caso)
#define LWIP_IPV4 1            // Habilita IPv4
#define NO_SYS 0               // Usa sistema operacional (necessário para thread-safety)
#define SYS_LIGHTWEIGHT_PROT 1  // Proteção leve para threads
#define MEM_LIBC_MALLOC 0      // Usa memória padrão do lwIP
#define MEMP_MEM_MALLOC 0      // Usa memória padrão do lwIP

#endif