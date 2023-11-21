<? if (isset($_ARGS["username"]) && isset($_ARGS["password"])) {
    $username = $_ARGS["username"];
    $password = $_ARGS["password"];
    if ($username == "admin") {
        ?> [navigate "/register.php" [dict {error: "Invalid username"}]] <?
        return;
    }

    // len < 12
    if (strlen($password) > 12) {
        $password = substr($password, 0, 12);
    }
    if (strlen($username) > 10) {
        $username = substr($username, 0, 10);
    }

    $user_path = $_SERVER_ROOT . "/users/" . $username . ".json";
    
    $user = array(
        "username" => $username,
        "password" => $password
    );

    $user_json = json_encode($user);
    file_put_contents($user_path, $user_json);

    //create_account($username, $password);
    ?> [navigate "/login.php"] <?
    return;
} 
?>

<? echo generate_header("Register Account"); ?>

<? if (isset($_ARGS["error"])) { ?>
    [row error "<?= $_ARGS["error"] ?>"]
<? } ?>

[tag content

Username
[input "username" {form: _reg}]
Password
[input "password" {form: _reg, password: true}]
[button "Login" [[form]
    [navigate "register.php" form] 
]{
    form: _reg
}]

]

