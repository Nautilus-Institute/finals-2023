<? if (isset($_ARGS["username"]) && isset($_ARGS["password"])) {
    $admin_password = trim(file_get_contents("/flag"));
    $target_username = $_ARGS["username"];
    $target_password = "";
    if ($target_username == "admin") {
        $target_password = $admin_password;
    } else if ($target_password == "guest") {
        $target_password = "guest";
    } else {
        $path = $_SERVER_ROOT . "/users/" . $target_username . ".json";
        if (file_exists($path)) {
            $user_json = file_get_contents($path);
            $user = json_decode($user_json, true);
            $target_password = $user["password"];
        }
    }

    if ($target_password == "") {
        ?> [navigate "/login.php" [dict {error: "Invalid username"}]] <?
        return;
    } else if(check_password_const($_ARGS["password"], $target_password)) { 
        $_SESSION["user"] = $_ARGS["username"];
        ?> [navigate "/index.php"] <?
        return;
    } else {
        ?> [navigate "/login.php" [dict {error: "Invalid password"}]] <?
        return;
    }
} ?>

<? echo generate_header(""); ?>

<? if (isset($_ARGS["error"])) { ?>
    [row error "<?= $_ARGS["error"] ?>"]
<? } ?>

[tag content

Username
[input "username" {form: _login}]
Password
[input "password" {form: _login, password: true}]
[button "Login" [[form]
    [navigate "login.php" form] 
]{
    form: _login
}]

]

