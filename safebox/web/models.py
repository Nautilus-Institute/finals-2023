from typing import Optional
import datetime

from django.db import models


class SecretFlag(models.Model):
    flag = models.CharField(max_length=200)
    tick_id = models.IntegerField(null=False)  # private tick ID
    timestamp = models.DateTimeField(auto_now=True)


class Submission(models.Model):
    team_id = models.IntegerField(null=False)
    tick_id = models.IntegerField(null=False)  # private tick ID
    prompt_type = models.IntegerField(null=False)  # 0 -
    prompt = models.TextField(null=False)
    timestamp = models.DateTimeField(auto_now=True)

    @staticmethod
    def latest_submission(team_id, prompt_type) -> Optional["Submission"]:
        submission = Submission.objects.filter(team_id=team_id, prompt_type=prompt_type).order_by("-timestamp").first()
        return submission

    @staticmethod
    def latest_submission_before(team_id, prompt_type, before: datetime.datetime) -> Optional["Submission"]:
        submission = Submission.objects.filter(team_id=team_id, prompt_type=prompt_type, timestamp__lt=before).order_by("-timestamp").first()
        return submission


class AIConversation(models.Model):
    tick_id = models.IntegerField(null=False)
    attack_team_id = models.IntegerField(null=False)
    defense_team_id = models.IntegerField(null=False)
    conversation = models.TextField()
    timestamp = models.DateTimeField(auto_now=True)
    result = models.IntegerField(null=False)  # win, lose, and potentially tie
    secret = models.CharField(null=False, max_length=20)


class RankResult(models.Model):
    tick_id = models.IntegerField(null=False)
    result = models.CharField(null=False, max_length=20000)


class Team(models.Model):
    team_id = models.IntegerField(null=False)
    team_name = models.CharField(max_length=120, null=False)
    password = models.CharField(max_length=120, null=False)
