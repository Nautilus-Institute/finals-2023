from django.urls import path

from . import views

urlpatterns = [
    path("", views.index, name="index"),
    path("login", views.login, name="login"),
    path("logout", views.logout, name="logout"),
    path("submit_prompt", views.submit_prompt, name="submit_prompt"),
    path("get_tick_result", views.get_tick_result, name="get_tick_result"),
    path("get_conversation", views.get_conversation, name="get_conversation"),

    # for the safebox bot
    path("bot/all_conversations", views.get_all_conversations, name="get_all_conversations"),
    path("bot/submit_response", views.submit_response, name="submit_response"),
    path("bot/get_ranking_result", views.get_ranking_result, name="get_ranking_result"),
]