
#include <string.h>
#include <stdlib.h>
#include "jsmn.h"

/**
 * @brief Parse edilmiş token dizisi içinde anahtar arar ve değerini kopyalar.
 *
 * @param json_str Ham JSON metni (indislerin referans alacağı kaynak)
 * @param tokens   Ana programda parse edilmiş token dizisi
 * @param token_count jsmn_parse tarafından dönen toplam token sayısı
 * @param target_key  Aranan anahtar (örn: "addr")
 * @param value       Bulunan değerin yazılacağı buffer
 * @param value_size  value buffer'ının toplam boyutu (null sonlandırıcı dahil)
 * @return int        Başarılıysa 0, bulunamazsa ya da değer buffer'a sığmazsa -1
 */
int json_get_value(const char *json_str, jsmntok_t *tokens, int token_count, const char *target_key, char *value, size_t value_size) {

    // i=1'den başlıyoruz (tokens[0] genelde tüm objedir)
    for (int i = 1; i < token_count; i++) {
        int tok_len = tokens[i].end - tokens[i].start;

        // Anahtar eşleşmesi kontrolü
        if (tokens[i].type == JSMN_STRING &&
            (int)strlen(target_key) == tok_len &&
            strncmp(json_str + tokens[i].start, target_key, tok_len) == 0) {

            // Değer bir sonraki tokendır (i+1)
            jsmntok_t *v = &tokens[i + 1];
            int val_len = v->end - v->start;
            if (val_len < 0 || (size_t)val_len >= value_size) return -1; // buffer'a sığmıyor

            memcpy(value, json_str + v->start, val_len);
            value[val_len] = '\0';

            return 0; // Bulundu
        }
    }
    return -1; // Bulunamadı
}

void json_get_string(const char *json_str, jsmntok_t *tokens, int token_count, const char *target_key, char *value)
{
    char result[25];
    if (json_get_value(json_str, tokens, token_count, target_key, result, sizeof(result)) == 0)
    {
        strcpy(value, result);
    } else memset(value, 0, 25);
}

void json_get_int(const char *json_str, jsmntok_t *tokens, int token_count, const char *target_key, uint8_t *value)
{
    char result[25];
    if (json_get_value(json_str, tokens, token_count, target_key, result, sizeof(result)) == 0)
    {
        *value = atoi(result);
    } else *value = 0xFF;
}