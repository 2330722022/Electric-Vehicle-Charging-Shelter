package com.example.onenet215;

import java.io.UnsupportedEncodingException;
import java.net.URLEncoder;
import java.security.InvalidKeyException;
import java.security.NoSuchAlgorithmException;
import java.util.Base64;

import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;

/**
 * OneNET Token 生成器
 * 用于生成动态 token 进行 MQTT 连接认证
 */
public class TokenGenerator {

    private static final String VERSION = "2020-05-29";
    private static final String SIGNATURE_METHOD = "sha1";
    private static final long EXPIRATION_TIME = 1956499200L; // 2032年过期

    private String userId;
    private String accessKey;

    public TokenGenerator(String userId, String accessKey) {
        this.userId = userId;
        this.accessKey = accessKey;
    }

    /**
     * 组装完整的 token
     */
    public String generateToken() throws UnsupportedEncodingException, NoSuchAlgorithmException, InvalidKeyException {
        String resourceName = "userid/" + userId;
        String expirationTime = String.valueOf(EXPIRATION_TIME);
        
        StringBuilder sb = new StringBuilder();
        String res = URLEncoder.encode(resourceName, "UTF-8");
        String sig = URLEncoder.encode(generatorSignature(resourceName, expirationTime), "UTF-8");
        
        sb.append("version=").append(VERSION)
          .append("&res=").append(res)
          .append("&et=").append(expirationTime)
          .append("&method=").append(SIGNATURE_METHOD)
          .append("&sign=").append(sig);
        
        return sb.toString();
    }

    /**
     * 生成签名
     */
    private String generatorSignature(String resourceName, String expirationTime)
            throws NoSuchAlgorithmException, InvalidKeyException {
        String encryptText = expirationTime + "\n" + SIGNATURE_METHOD + "\n" + resourceName + "\n" + VERSION;
        byte[] bytes = HmacEncrypt(encryptText, accessKey);
        return Base64.getEncoder().encodeToString(bytes);
    }

    /**
     * HMAC加密
     */
    private byte[] HmacEncrypt(String data, String key) throws NoSuchAlgorithmException, InvalidKeyException {
        SecretKeySpec signinKey = new SecretKeySpec(Base64.getDecoder().decode(key), "HmacSHA1");
        Mac mac = Mac.getInstance("HmacSHA1");
        mac.init(signinKey);
        return mac.doFinal(data.getBytes());
    }
}
