package com.example.onenet215;

import android.os.Bundle;
import android.text.TextUtils;
import android.view.View;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.button.MaterialButton;
import com.google.android.material.textfield.TextInputEditText;

import java.util.concurrent.Executor;
import java.util.concurrent.Executors;

public class RegisterActivity extends AppCompatActivity {

    private TextInputEditText etRegUsername, etRegPassword, etRegConfirm;
    private MaterialButton btnRegister;
    private TextView tvRegStatus, tvBackToLogin;
    private final Executor ioExecutor = Executors.newSingleThreadExecutor();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_register);

        etRegUsername = findViewById(R.id.etRegUsername);
        etRegPassword = findViewById(R.id.etRegPassword);
        etRegConfirm = findViewById(R.id.etRegConfirm);
        btnRegister = findViewById(R.id.btnRegister);
        tvRegStatus = findViewById(R.id.tvRegStatus);
        tvBackToLogin = findViewById(R.id.tvBackToLogin);

        tvBackToLogin.setOnClickListener(v -> finish());

        btnRegister.setOnClickListener(v -> performRegister());
    }

    private void performRegister() {
        String username = etRegUsername.getText() != null ? etRegUsername.getText().toString().trim() : "";
        String password = etRegPassword.getText() != null ? etRegPassword.getText().toString().trim() : "";
        String confirm = etRegConfirm.getText() != null ? etRegConfirm.getText().toString().trim() : "";

        if (TextUtils.isEmpty(username)) {
            showStatus("请输入账号");
            return;
        }
        if (username.length() < 2) {
            showStatus("账号至少2个字符");
            return;
        }
        if (TextUtils.isEmpty(password)) {
            showStatus("请输入密码");
            return;
        }
        if (password.length() < 4) {
            showStatus("密码至少4位");
            return;
        }
        if (!password.equals(confirm)) {
            showStatus("两次密码不一致");
            return;
        }

        btnRegister.setEnabled(false);
        btnRegister.setText("注册中...");
        tvRegStatus.setVisibility(View.GONE);

        ioExecutor.execute(() -> {
            AppDatabase db = AppDatabase.getInstance(this);
            User existing = db.userDao().findByUsername(username);

            if (existing != null) {
                runOnUiThread(() -> {
                    btnRegister.setEnabled(true);
                    btnRegister.setText("注  册");
                    showStatus("该账号已存在，请更换");
                });
                return;
            }

            User newUser = new User();
            newUser.username = username;
            newUser.password = password;
            newUser.role = "user";
            db.userDao().insert(newUser);

            runOnUiThread(() -> {
                btnRegister.setEnabled(true);
                btnRegister.setText("注  册");
                Toast.makeText(this, "注册成功，请返回登录", Toast.LENGTH_SHORT).show();
                finish();
            });
        });
    }

    private void showStatus(String msg) {
        tvRegStatus.setText(msg);
        tvRegStatus.setVisibility(View.VISIBLE);
    }
}
